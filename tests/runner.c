/*
 * STEEL CHAIN ヘッドレステストランナー (libmgba 使用)
 *
 *   runner <rom.gba> <outdir> [scenario]
 *
 * scenario:
 *   smoke  (既定) タイトル→ステージ開始→自動プレイ。クラッシュ/停止を検出し、
 *          スクリーンショットを outdir に PNG で保存する。
 *   bot    全ステージを自動プレイ (到達度を報告、失敗扱いにはしない)
 *
 * ROM 側は EWRAM 先頭 (0x02000000) にデバッグブロック g_debug を置いている。
 */
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/config.h>
#include <mgba/core/blip_buf.h>
#include <math.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/stat.h>

#define KA 0x001
#define KB 0x002
#define KSEL 0x004
#define KSTART 0x008
#define KRIGHT 0x010
#define KLEFT 0x020
#define KUP 0x040
#define KDOWN 0x080
#define KR 0x100
#define KL 0x200

enum { GS_TITLE, GS_STAGE_INTRO, GS_PLAY, GS_DEAD, GS_CLEAR, GS_GAMEOVER, GS_ENDING, GS_PAUSE };
static const char* state_names[] = {"TITLE", "INTRO", "PLAY", "DEAD", "CLEAR", "GAMEOVER", "ENDING", "PAUSE"};

typedef struct {
    uint32_t magic, frame, state, stage;
    int32_t px, py, hp, lives;
    uint32_t score, kills, deaths, chain_best, enemies, cam_x, clears;
} DebugInfo;

static void quiet_log(struct mLogger* l, int cat, enum mLogLevel level, const char* fmt, va_list args) {
    (void)l;
    (void)cat;
    if (level & (mLOG_FATAL | mLOG_ERROR)) {
        vfprintf(stderr, fmt, args);
        fputc('\n', stderr);
    }
}
static struct mLogger logger = {.log = quiet_log};

static struct mCore* core;
static color_t* fb;
static unsigned W, H;
static const char* outdir;
static int failures;
static uint32_t frames_run;

static void read_debug(DebugInfo* d) {
    uint32_t* p = (uint32_t*)d;
    for (size_t i = 0; i < sizeof(DebugInfo) / 4; i++) p[i] = core->busRead32(core, 0x02000000 + i * 4);
}

/* ---- 音声キャプチャ ---- */
#define AUDIO_RATE 32768
static short* audio_buf;
static size_t audio_len, audio_cap;
static int audio_capturing;

static void audio_drain(void) {
    blip_t* l = core->getAudioChannel(core, 0);
    blip_t* r = core->getAudioChannel(core, 1);
    short tmp[2048], tmpr[2048];
    int n;
    while ((n = blip_samples_avail(l)) > 0) {
        if (n > 2048) n = 2048;
        blip_read_samples(l, tmp, n, 0);
        blip_read_samples(r, tmpr, n, 0);
        if (!audio_capturing) continue;
        if (audio_len + n > audio_cap) {
            audio_cap = (audio_len + n) * 2;
            audio_buf = realloc(audio_buf, audio_cap * sizeof(short));
        }
        for (int i = 0; i < n; i++) audio_buf[audio_len++] = (short)((tmp[i] + tmpr[i]) / 2);
    }
}

static void run(int n, uint32_t keys) {
    core->setKeys(core, keys);
    for (int i = 0; i < n; i++) {
        core->runFrame(core);
        audio_drain();
        frames_run++;
    }
}

static void audio_begin(void) {
    audio_len = 0;
    audio_capturing = 1;
}

/* WAV を保存し RMS を返す */
static double audio_end(const char* name) {
    audio_capturing = 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.wav", outdir, name);
    FILE* f = fopen(path, "wb");
    uint32_t bytes = (uint32_t)(audio_len * 2);
    uint32_t hdr32;
    uint16_t hdr16;
    fwrite("RIFF", 1, 4, f);
    hdr32 = 36 + bytes; fwrite(&hdr32, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    hdr32 = 16; fwrite(&hdr32, 4, 1, f);
    hdr16 = 1; fwrite(&hdr16, 2, 1, f);
    hdr16 = 1; fwrite(&hdr16, 2, 1, f);
    hdr32 = AUDIO_RATE; fwrite(&hdr32, 4, 1, f);
    hdr32 = AUDIO_RATE * 2; fwrite(&hdr32, 4, 1, f);
    hdr16 = 2; fwrite(&hdr16, 2, 1, f);
    hdr16 = 16; fwrite(&hdr16, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fwrite(audio_buf, 2, audio_len, f);
    fclose(f);
    double sum = 0, mean = 0;
    for (size_t i = 0; i < audio_len; i++) mean += audio_buf[i];
    mean /= audio_len ? audio_len : 1;
    for (size_t i = 0; i < audio_len; i++) sum += (audio_buf[i] - mean) * (audio_buf[i] - mean);
    double rms = audio_len ? sqrt(sum / audio_len) : 0;
    printf("  [wave] %s (%.1fs, rms=%.0f)\n", path, audio_len / (double)AUDIO_RATE, rms);
    return rms;
}

static void png_chunk(FILE* f, const char* type, const unsigned char* data, uint32_t len) {
    unsigned char hdr[8] = {len >> 24, len >> 16, len >> 8, len, type[0], type[1], type[2], type[3]};
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc32(0, hdr + 4, 4);
    if (len) crc = crc32(crc, data, len);
    unsigned char c[4] = {crc >> 24, crc >> 16, crc >> 8, crc};
    fwrite(c, 1, 4, f);
}

static void screenshot(const char* name) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", outdir, name);
    size_t rowlen = 1 + W * 3;
    unsigned char* raw = malloc(rowlen * H);
    for (unsigned y = 0; y < H; y++) {
        raw[y * rowlen] = 0;
        for (unsigned x = 0; x < W; x++) {
            uint32_t c = fb[y * W + x];
            /* mGBA の color_t (32bit) は XBGR8 */
            raw[y * rowlen + 1 + x * 3 + 0] = c & 0xFF;
            raw[y * rowlen + 1 + x * 3 + 1] = (c >> 8) & 0xFF;
            raw[y * rowlen + 1 + x * 3 + 2] = (c >> 16) & 0xFF;
        }
    }
    uLongf zlen = compressBound(rowlen * H);
    unsigned char* z = malloc(zlen);
    compress2(z, &zlen, raw, rowlen * H, 9);
    FILE* f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    unsigned char ihdr[13] = {W >> 24, W >> 16, W >> 8, W, H >> 24, H >> 16, H >> 8, H, 8, 2, 0, 0, 0};
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, zlen);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    printf("  [shot] %s\n", path);
}

static void check(int cond, const char* msg) {
    if (cond) {
        printf("  [ ok ] %s\n", msg);
    } else {
        printf("  [FAIL] %s\n", msg);
        failures++;
    }
}

static void print_debug(const char* label) {
    DebugInfo d;
    read_debug(&d);
    printf("  %-14s frame=%u state=%s stage=%u pos=(%d,%d) hp=%d lives=%d score=%u kills=%u deaths=%u chain=%u clears=%u\n",
           label, d.frame, d.state < 8 ? state_names[d.state] : "?", d.stage, d.px, d.py, d.hp, d.lives, d.score,
           d.kills, d.deaths, d.chain_best, d.clears);
}

/* 簡易ボット: 右へ進みつつ斬り、詰まったらジャンプ/ダッシュ */
static void bot_step(int t, DebugInfo* prev, int* stuck) {
    DebugInfo d;
    read_debug(&d);
    uint32_t keys = 0;
    if (d.state == GS_PLAY || d.state == GS_DEAD) {
        keys |= KRIGHT;
        if (t % 8 < 2) keys |= KB;
        if (d.px == prev->px) (*stuck)++;
        else *stuck = 0;
        if (*stuck > 6 || t % 47 < 12) keys |= KA;
        if (*stuck > 30 && t % 60 == 0) keys |= KR;
        if ((t / 300) % 4 == 3 && t % 300 < 30) keys = KLEFT | (t % 8 < 2 ? KB : 0); /* たまに振り返る */
    } else if (d.state == GS_TITLE || d.state == GS_CLEAR || d.state == GS_GAMEOVER || d.state == GS_STAGE_INTRO) {
        if (t % 20 == 0) keys |= KSTART;
    }
    *prev = d;
    run(1, keys);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s rom.gba outdir [smoke|bot]\n", argv[0]);
        return 2;
    }
    const char* rom = argv[1];
    outdir = argv[2];
    const char* scenario = argc > 3 ? argv[3] : "smoke";
    mkdir(outdir, 0755);
    mLogSetDefaultLogger(&logger);

    core = mCoreFind(rom);
    if (!core || !core->init(core)) {
        fprintf(stderr, "mGBA core init failed\n");
        return 2;
    }
    mCoreInitConfig(core, NULL);
    struct mCoreOptions opts = {.useBios = false, .skipBios = true, .volume = 0x100};
    mCoreConfigLoadDefaults(&core->config, &opts);
    core->desiredVideoDimensions(core, &W, &H);
    fb = calloc(W * H, sizeof(color_t));
    core->setVideoBuffer(core, fb, W);
    if (!mCoreLoadFile(core, rom)) {
        fprintf(stderr, "ROM load failed: %s\n", rom);
        return 2;
    }
    mCoreLoadConfig(core);
    core->setAudioBufferSize(core, 4096);
    blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core), AUDIO_RATE);
    blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core), AUDIO_RATE);
    core->reset(core);

    DebugInfo d;
    printf("== scenario: %s ==\n", scenario);

    /* 共通: タイトル (BGM を録音) */
    audio_begin();
    run(90, 0);
    read_debug(&d);
    check(d.magic == 0x48435453u, "デバッグブロック(magic)が存在する");
    check(d.state == GS_TITLE, "起動後タイトル画面にいる");
    check(d.frame >= 80, "フレームカウンタが進んでいる (ゲームループ稼働)");
    screenshot("01_title");
    if (strcmp(scenario, "smoke") == 0) run(600, 0);
    check(audio_end("bgm_title") > 200.0, "タイトルで BGM が鳴っている");

    run(2, KSTART);
    run(40, 0);
    read_debug(&d);
    check(d.state == GS_STAGE_INTRO, "START でステージ紹介へ遷移");
    screenshot("02_intro");
    run(100, 0);
    read_debug(&d);
    check(d.state == GS_PLAY, "ステージ開始 (PLAY)");
    int start_x = d.px;
    print_debug("start");

    if (strcmp(scenario, "smoke") == 0) {
        audio_begin();
        /* 右へ走る */
        run(50, KRIGHT);
        read_debug(&d);
        check(d.px > start_x + 30, "右入力で前進する");
        screenshot("03_run");
        /* ジャンプ */
        int ground_y = d.py;
        run(12, KA | KRIGHT);
        read_debug(&d);
        check(d.py < ground_y - 16, "A でジャンプする");
        run(40, KRIGHT);
        /* コンボ */
        for (int i = 0; i < 3; i++) {
            run(2, KB);
            run(5, 0);
        }
        screenshot("04_combo");
        run(30, 0);
        /* ダッシュ */
        read_debug(&d);
        int bx = d.px;
        run(2, KR | KRIGHT);
        run(10, KRIGHT);
        read_debug(&d);
        check(d.px > bx + 30, "R でダッシュする");
        run(400, 0);
        check(audio_end("bgm_stage") > 200.0, "ステージで BGM/効果音が鳴っている");
        /* 自動プレイで 30 秒 */
        DebugInfo prev = d;
        int stuck = 0;
        uint32_t last_frame = d.frame;
        for (int t = 0; t < 1800; t++) {
            bot_step(t, &prev, &stuck);
            if (t % 600 == 599) {
                char name[64];
                snprintf(name, sizeof(name), "05_play_%02d", t / 600);
                screenshot(name);
                read_debug(&d);
                check(d.frame > last_frame, "ゲームループが停止していない");
                last_frame = d.frame;
            }
        }
        print_debug("after bot");
        read_debug(&d);
        check(d.kills > 0 || d.score > 0, "敵を倒せる/スコアが入る");
    } else {
        DebugInfo prev = d;
        int stuck = 0;
        uint32_t max_stage = 0;
        for (int t = 0; t < 60 * 60 * 6; t++) {
            bot_step(t, &prev, &stuck);
            if (prev.stage > max_stage) max_stage = prev.stage;
            if (t % 1800 == 0) {
                char name[64];
                snprintf(name, sizeof(name), "bot_%03d", t / 1800);
                screenshot(name);
                print_debug("bot");
            }
            if (prev.state == GS_ENDING) {
                printf("  ボットがエンディングに到達\n");
                screenshot("bot_ending");
                break;
            }
        }
        print_debug("bot end");
        printf("  到達ステージ: %u\n", max_stage + 1);
    }

    print_debug("final");
    printf("== %s: %d failure(s), %u frames ==\n", failures ? "FAILED" : "PASSED", failures, frames_run);
    core->deinit(core);
    return failures ? 1 : 0;
}
