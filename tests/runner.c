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
#include <stddef.h>
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
    uint32_t level_cells, level_w, level_h;
    int32_t vy;
    uint32_t on_ground;
    int32_t boss_hp;
    uint32_t boss_state;
    int32_t boss_x;
    uint32_t cmd, cmd_arg;
    uint32_t lift_count;
    int32_t lifts[8];
    uint32_t obj_tiles;
    uint32_t lag_frames, max_lines;
    uint32_t max_state, lag_state;
    uint32_t pwaves, charge;
    uint32_t ending_phase;
    uint32_t popups;
    uint32_t rank, rank_hist;
    uint32_t ambient;
    uint32_t best_time;
    uint32_t combo;
    uint32_t checkpoints;
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

static void run(int n, uint32_t keys);
static void read_debug(DebugInfo* d);
/* 指定状態になるまで最大 max フレーム進める */
static void wait_state(uint32_t st, int max) {
    DebugInfo d;
    for (int i = 0; i < max; i++) {
        read_debug(&d);
        if (d.state == st) return;
        run(1, 0);
    }
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

/* ステージ情報を読むボット: 穴・トゲ・壁の手前でジャンプし、斬りながら右へ進む */
static int cell_at(const DebugInfo* d, int cx, int cy) {
    if (!d->level_cells || cx < 0 || cx >= (int)d->level_w) return 1;
    if (cy < 0 || cy >= (int)d->level_h) return 0;
    return core->busRead8(core, d->level_cells + cy * d->level_w + cx) & 0x0F;
}
#define C_SOLID 1
#define C_ONEWAY 2
#define C_HAZARD 3

static int bot_jump_hold;

/* 地面 (固体/足場) の上面か */
static int is_floor(int c) { return c == C_SOLID || c == C_ONEWAY; }

/* 前方に届きそうな動く足場があるか: 1=今跳べば乗れる, 2=待てば来る, 0=無い */
static int lift_ahead(const DebugInfo* d, int feet) {
    int front = d->px + 10;
    int any = 0;
    for (unsigned i = 0; i < d->lift_count && i < 4; i++) {
        int lx = d->lifts[i * 2], ly = d->lifts[i * 2 + 1];
        if (lx + 32 < d->px - 8 || lx > d->px + 220) continue;
        any = 1;
        int dy = ly - feet;
        /* 同じ高さで縁に接していれば歩いて乗る */
        if (dy > -3 && dy < 3 && lx <= front + 2 && lx + 32 >= front + 12) return 3;
        /* 高さが違う足場は縁付近に来たら跳ぶ */
        if (dy <= -3 && lx <= front + 20 && lx + 32 >= front + 12 && dy > -48) return 1;
        /* 低い位置の足場: 真下付近に来たら飛び降りる */
        if (dy >= 3 && dy < 80 && lx <= front + 24 && lx + 32 >= front + 4) return 1;
    }
    return any ? 2 : 0;
}

/* 動く足場の上にいるとき: 前方の着地点が射程内か */
static int landing_in_reach(const DebugInfo* d, int feet) {
    int cx0 = (d->px + 5) >> 4;
    for (int a = 1; a <= 4; a++) {
        for (int cy = (feet >> 4) - 4; cy <= (feet >> 4) + 2; cy++) {
            if (is_floor(cell_at(d, cx0 + a, cy)) && !is_floor(cell_at(d, cx0 + a, cy - 1)) &&
                cell_at(d, cx0 + a, cy - 1) != C_HAZARD) {
                int top = cy * 16;
                int gap = (cx0 + a) * 16 - (d->px + 10);
                int dh = top - feet; /* 負 = 高い */
                int allow = dh >= 0 ? 44 : 44 + dh / 2;
                if (dh > -40 && dh < 64 && gap <= allow) return 1;
            }
        }
    }
    for (unsigned i = 0; i < d->lift_count && i < 4; i++) {
        int lx = d->lifts[i * 2], ly = d->lifts[i * 2 + 1];
        if (lx > d->px + 20 && lx < d->px + 72 && ly - feet > -40 && ly - feet < 40) return 1;
    }
    return 0;
}

enum { BS_NONE, BS_INTRO, BS_IDLE, BS_SHOOT, BS_CHARGE, BS_STUN, BS_JUMP, BS_DEAD };

/* ボス戦の戦術: 突進は動き出してから飛び越し、着地の衝撃波は到達時間を見て跳ぶ、気絶中に連撃 */
static int wave_jump_at = -1;
static uint32_t boss_keys(const DebugInfo* d, const DebugInfo* prev, int t) {
    int pcx = d->px + 5;
    int dx = d->boss_x - pcx;
    int adx = dx < 0 ? -dx : dx;
    int moving = d->boss_x != prev->boss_x;
    uint32_t toward = dx > 0 ? KRIGHT : KLEFT;
    uint32_t away = dx > 0 ? KLEFT : KRIGHT;
    uint32_t keys = 0;
    if (prev->boss_state == BS_JUMP && d->boss_state != BS_JUMP) {
        /* 衝撃波 (約 2.1px/f) が足元に来る少し前にジャンプ */
        int eta = (adx - 14) * 256 / 0x220;
        wave_jump_at = t + (eta > 12 ? eta - 12 : 0);
    }
    if (wave_jump_at >= 0 && t >= wave_jump_at) {
        if (d->on_ground && bot_jump_hold == 0) bot_jump_hold = 24;
        wave_jump_at = -1;
    }
    switch (d->boss_state) {
    case BS_CHARGE:
        if (!moving) {
            /* 予備動作中: 距離を取る */
            keys = adx < 100 ? away : 0;
        } else {
            keys = 0;
            if (adx < 72 && d->on_ground && bot_jump_hold == 0) bot_jump_hold = 24;
            if (!d->on_ground) keys = toward;
        }
        break;
    case BS_JUMP:
        keys = adx < 60 ? away : 0;
        break;
    case BS_STUN:
        keys = adx > 26 ? toward : 0;
        if (t % 6 < 2) keys |= KB;
        break;
    default:
        /* 剣先が届く間合い (35〜44px) を保って斬る */
        keys = adx > 44 ? toward : 0;
        if (adx < 34) keys = away;
        if (adx < 50 && t % 18 < 2) keys = (keys & ~(KLEFT | KRIGHT)) | KB;
        break;
    }
    if (bot_jump_hold > 0) {
        bot_jump_hold--;
        if (bot_jump_hold < 23) keys |= KA;
    }
    return keys;
}

static void bot_step(int t, DebugInfo* prev, int* stuck) {
    DebugInfo d;
    read_debug(&d);
    uint32_t keys = 0;
    if (d.state == GS_PLAY && d.boss_hp > 0 && d.boss_state == BS_INTRO) {
        keys = 0;
    } else if (d.state == GS_PLAY && d.boss_hp > 0) {
        keys = boss_keys(&d, prev, t);
    } else if (d.state == GS_PLAY) {
        keys |= KRIGHT;
        if (d.px == prev->px) (*stuck)++;
        else *stuck = 0;
        int feet = d.py + 23;
        int cx = (d.px + 5) >> 4;
        int cy = feet >> 4;
        int want_jump = 0;
        int gap_near = 0;
        for (int a = 1; a <= 4; a++) {
            int below = cell_at(&d, cx + a, cy + 1);
            if (below != C_SOLID && below != C_ONEWAY) gap_near = 1;
        }
        /* 穴の近くでは攻撃しない (攻撃で足が止まるため) */
        if (!gap_near && t % 10 < 2) keys |= KB;
        /* 実際に動く足場の上に立っているか */
        int on_lift = 0;
        if (d.on_ground)
            for (unsigned i = 0; i < d.lift_count && i < 4; i++) {
                int lx = d.lifts[i * 2], ly = d.lifts[i * 2 + 1];
                if (ly - (feet + 1) > -3 && ly - (feet + 1) < 3 && d.px + 10 > lx && d.px < lx + 32) on_lift = 1;
            }
        if (on_lift) {
            /* 足場の上: 着地点が射程に入るまで待つ */
            keys &= ~KRIGHT;
            keys &= ~KB;
            /* 乗っている足場の右端へ寄る */
            int at_edge = 1;
            for (unsigned i = 0; i < d.lift_count && i < 4; i++) {
                int lx = d.lifts[i * 2], ly = d.lifts[i * 2 + 1];
                if (ly - feet > -3 && ly - feet < 4 && d.px + 10 > lx && d.px < lx + 32 && d.px + 10 < lx + 29) {
                    keys |= KRIGHT;
                    at_edge = 0;
                }
            }
            *stuck = 0;
            if (at_edge && landing_in_reach(&d, feet)) {
                want_jump = 1;
                keys |= KRIGHT;
            }
        } else if (d.on_ground) {
            /* 足元の 1 セル先が穴/トゲ, 前方が壁なら跳ぶ */
            int x_front = d.px + 10 + 4;
            int below = cell_at(&d, x_front >> 4, cy + 1);
            /* 段差 (下に床がある) か本当の穴かを区別 */
            int drop_ok = 0;
            for (int k = 1; k <= 5; k++) {
                int c2 = cell_at(&d, x_front >> 4, cy + k);
                if (is_floor(c2)) { drop_ok = 1; break; }
                if (c2 == C_HAZARD) break;
            }
            if (below != C_SOLID && below != C_ONEWAY && !drop_ok) {
                int la = lift_ahead(&d, feet);
                if (la == 0) want_jump = 1;
                else if (la == 3) { /* 同じ高さで隣接: そのまま歩いて乗る */
                    *stuck = 0;
                    keys &= ~KB;
                }
                else if (la == 1) want_jump = 1;
                else {
                    keys &= ~KRIGHT; /* 足場を待つ (寄ってくる敵は斬る) */
                    if (t % 12 < 2 && d.enemies > 0) keys |= KB;
                    *stuck = 0;
                }
            }
            if (below == C_HAZARD) want_jump = 1;
            int front = (d.px + 10 + 6) >> 4;
            if (cell_at(&d, front, cy) == C_SOLID || cell_at(&d, front, cy - 1) == C_SOLID) want_jump = 1;
            if (*stuck > 4) want_jump = 1;
        }
        if (getenv("TRACE") && d.px > atoi(getenv("TRACE")) && d.px < atoi(getenv("TRACE")) + 320) printf("  [tr] t=%d px=%d py=%d og=%u lift=%d keys=%x L0=(%d,%d) L1=(%d,%d) hp=%d\n", t, d.px, d.py, d.on_ground, on_lift, keys, d.lifts[0], d.lifts[1], d.lifts[2], d.lifts[3], d.hp);
        if (getenv("JUMP_LOG") && want_jump && bot_jump_hold == 0) printf("  [jump] px=%d on_lift=%d stuck=%d below=%d\n", d.px, on_lift, *stuck, cell_at(&d, (d.px + 14) >> 4, cy + 1));
        if (want_jump && bot_jump_hold == 0) bot_jump_hold = 22;
        if (bot_jump_hold > 0) {
            bot_jump_hold--;
            if (bot_jump_hold < 21) keys |= KA; /* 1 フレーム離してから押す */
        }
        if (*stuck > 40 && t % 30 == 0) keys |= KR;
    } else if (d.state == GS_TITLE || d.state == GS_CLEAR || d.state == GS_GAMEOVER || d.state == GS_STAGE_INTRO) {
        if (t % 20 == 0) keys |= KSTART;
    }
    if (d.state == GS_DEAD && prev->state != GS_DEAD) printf("  [death] frame=%u pos=(%d,%d) cell=(%d,%d) hp=%d\n", d.frame, prev->px, prev->py, (prev->px + 5) >> 4, (prev->py + 23) >> 4, prev->hp);
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
    printf("  OBJ タイル使用量: %u / 1024\n", d.obj_tiles);
    check(d.obj_tiles > 0 && d.obj_tiles <= 1024, "スプライト VRAM (1024 タイル) に収まっている");
    screenshot("01_title");
    if (strcmp(scenario, "smoke") == 0) run(600, 0);
    check(audio_end("bgm_title") > 200.0, "タイトルで BGM が鳴っている");

    if (strcmp(scenario, "attract") == 0) {
        /* タイトル放置でデモプレイが始まり、ボタンでタイトルに戻る (EASY を選んでおく) */
        run(2, KLEFT);
        run(4, 0);
        run(1300, 0);
        wait_state(GS_PLAY, 400);
        read_debug(&d);
        check(d.state == GS_PLAY, "タイトルで放置するとデモプレイが始まる");
        int x0 = d.px;
        run(300, 0);
        read_debug(&d);
        screenshot("attract_demo");
        check(d.px > x0 + 100, "デモプレイでキャラクターが自動で進む");
        run(2, KA);
        run(5, 0);
        read_debug(&d);
        check(d.state == GS_TITLE, "ボタンを押すとデモが終わりタイトルに戻る");
        /* 2 回目のデモはステージ2 */
        run(1300, 0);
        wait_state(GS_PLAY, 400);
        read_debug(&d);
        check(d.state == GS_PLAY && d.stage == 1, "2 回目のデモはステージ2 で流れる");
        screenshot("attract_demo2");
        run(2, KA);
        run(30, 0);
        /* デモ前に選んだ難易度 (EASY) が保たれている */
        run(2, KSTART);
        run(40, 0);
        wait_state(GS_PLAY, 400);
        read_debug(&d);
        check(d.hp == 12, "デモの後も選んでいた難易度 (EASY) が保たれる");
        printf("== %s: %d failure(s), %u frames ==\n", failures ? "FAILED" : "PASSED", failures, frames_run);
        return failures ? 1 : 0;
    }
    if (strstr(scenario, "easy")) {
        /* EASY を選択 (NORMAL から左) */
        run(2, KLEFT);
        run(4, 0);
    }
    if (strstr(scenario, "hard")) {
        /* HARD を選択 (NORMAL から右) */
        run(2, KRIGHT);
        run(4, 0);
        screenshot("01b_title_hard");
    }
    run(2, KSTART);
    run(40, 0);
    read_debug(&d);
    check(d.state == GS_STAGE_INTRO, "START でステージ紹介へ遷移");
    screenshot("02_intro");
    run(110, 0);
    screenshot("02b_intro_msg");
    wait_state(GS_PLAY, 400);
    read_debug(&d);
    check(d.state == GS_PLAY, "ステージ開始 (PLAY)");
    int start_x = d.px;
    print_debug("start");
    if (strstr(scenario, "easy")) check(d.hp == 12, "EASY は HP 12 で始まる");
    else if (strstr(scenario, "hard")) check(d.hp == 6, "HARD は HP 6 で始まる");
    else check(d.hp == 8, "NORMAL は HP 8 で始まる");

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
        /* ダッシュ中に B でダッシュ斬り */
        run(30, 0);
        run(2, KR | KRIGHT);
        run(3, KRIGHT);
        run(2, KB | KRIGHT);
        read_debug(&d);
        check(d.combo == 0x104, "ダッシュ中に B でダッシュ斬りが出る");
        /* ダッシュの無敵時間中は点滅するので、表示されるフレームまで進めて撮影 */
        run(3, KRIGHT);
        screenshot("04a_dashslash");
        /* チャージ斬り: B 長押し → 離すと斬撃波 */
        run(20, 0);
        run(50, KB);
        read_debug(&d);
        check(d.charge >= 40, "B 長押しでチャージが溜まる");
        run(1, 0);
        run(3, 0);
        read_debug(&d);
        check(d.pwaves >= 1, "離すと斬撃波が飛ぶ");
        screenshot("04b_charge");
        /* ポーズメニュー: START で開き、下→A で「ステージやり直し」 */
        run(30, 0);
        run(2, KSTART);
        run(10, 0);
        read_debug(&d);
        check(d.state == GS_PAUSE, "START でポーズメニューが開く");
        screenshot("04c_pause");
        run(2, KSTART);
        run(5, 0);
        read_debug(&d);
        check(d.state == GS_PLAY, "START でゲームに戻る");
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
    } else if (strncmp(scenario, "demo", 4) == 0) {
        /* demoS_X: ステージ S の X からボットでプレイし、3 フレームごとに連番 PNG を保存 (GIF 用) */
        int st = atoi(scenario + 4) - 1;
        const char* us = strchr(scenario, '_');
        int x = us ? atoi(us + 1) : 0;
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), st);
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 1);
        run(30, 0);
        wait_state(GS_PLAY, 400);
        if (x) {
            core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), x);
            core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 2);
            run(40, 0);
        }
        read_debug(&d);
        DebugInfo prev = d;
        int stuck = 0;
        for (int t = 0; t < 600; t++) {
            bot_step(t, &prev, &stuck);
            if (t % 3 == 0) {
                char nm[64];
                snprintf(nm, sizeof(nm), "frame_%04d", t / 3);
                screenshot(nm);
            }
        }
    } else if (strncmp(scenario, "view", 4) == 0) {
        /* viewS_X: ステージ S の X ピクセル付近を撮影 (見た目確認用) */
        int st = atoi(scenario + 4) - 1;
        const char* us = strchr(scenario, '_');
        int x = us ? atoi(us + 1) : 0;
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), st);
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 1);
        run(30, 0);
        wait_state(GS_PLAY, 400);
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), x);
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 2);
        run(50, 0);
        screenshot(scenario);
        read_debug(&d);
        printf("  環境演出の粒: %u\n", d.ambient);
        for (int k = 0; k < 4; k++) {
            run(40, 0);
            char nm[80];
            snprintf(nm, sizeof(nm), "%s_%c", scenario, 'b' + k);
            screenshot(nm);
        }
    } else if (strcmp(scenario, "music") == 0) {
        /* 全曲を録音して WAV に保存 (試聴用) */
        static const char* names[] = {"title", "stage", "boss", "stage2", "final", "lastboss", "clear", "gameover", "ending"};
        for (int i = 0; i < 9; i++) {
            core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), i);
            core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 3);
            run(2, 0);
            audio_begin();
            run(i >= 6 ? 240 : 900, 0);
            char nm[64];
            snprintf(nm, sizeof(nm), "song_%d_%s", i + 1, names[i]);
            double rms = audio_end(nm);
            char msg[96];
            snprintf(msg, sizeof(msg), "曲 %s が鳴る", names[i]);
            check(rms > 200.0, msg);
        }
        /* 新しい効果音 (SFX_CHARGE〜SFX_GUARD = 11〜15) */
        static const char* sfx[] = {"charge", "wave", "laser", "alarm", "guard", "lowhp"};
        for (int i = 0; i < 6; i++) {
            run(30, 0);
            audio_begin();
            core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), 11 + i);
            core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 4);
            run(40, 0);
            char nm[64];
            snprintf(nm, sizeof(nm), "sfx_%s", sfx[i]);
            double rms = audio_end(nm);
            char msg[96];
            snprintf(msg, sizeof(msg), "効果音 %s が鳴る", sfx[i]);
            check(rms > 100.0, msg);
        }
    } else if (strncmp(scenario, "stage", 5) == 0) {
        /* 指定ステージ (1 始まり) からボットでプレイ */
        int st = atoi(scenario + 5) - 1;
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), st);
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 1);
        run(30, 0);
        wait_state(GS_PLAY, 400);
        DebugInfo prev = d;
        int stuck = 0;
        uint32_t clears0 = d.clears;
        for (int t = 0; t < 60 * 300; t++) {
            bot_step(t, &prev, &stuck);
            if (t % 300 == 0 && t < 9000) {
                char name[64];
                snprintf(name, sizeof(name), "st_%03d", t / 300);
                screenshot(name);
            }
            if (prev.clears > clears0 || prev.state == GS_ENDING) break;
            if (getenv("LIFT_LOG") && t % 8 == 0 && prev.px > 560 && prev.px < 760)
                printf("  [lift] vy=%d t=%d px=%d py=%d og=%u n=%u L0=(%d,%d) L1=(%d,%d)\n", prev.vy, t, prev.px, prev.py, prev.on_ground, prev.lift_count, prev.lifts[0], prev.lifts[1], prev.lifts[2], prev.lifts[3]);
        }
        read_debug(&d);
        print_debug("stage end");
        check(d.clears > clears0 || d.state == GS_ENDING, "ボットがこのステージをクリアできる");
        uint32_t tf = 0;
        if (d.clears > clears0) {
            run(60, 0);
            read_debug(&d);
            screenshot("stage_clear_rank");
            printf("  ランク: %c\n", "CBAS"[d.rank & 3]);
            check(d.rank_hist != 0, "クリア画面でランク評価が出る");
            check(d.checkpoints >= 1, "中間地点を通過すると CHECKPOINT が表示される");
            uint32_t cl = 0;
            for (int i = 0; i < 4; i++) cl |= (uint32_t)core->busRead8(core, 0x0E00000C + i) << (i * 8);
            uint32_t saved = (cl >> (16 + st * 3)) & 7;
            check(saved == (d.rank & 3) + 1, "ステージの最高ランクが SRAM に保存される");
            uint32_t tmagic = 0;
            tf = 0;
            for (int i = 0; i < 4; i++) tmagic |= (uint32_t)core->busRead8(core, 0x0E000020 + i) << (i * 8);
            tf = core->busRead8(core, 0x0E000024 + st * 2) | (core->busRead8(core, 0x0E000025 + st * 2) << 8);
            printf("  ベストタイム: %u.%02u 秒\n", tf / 60, (tf % 60) * 100 / 60);
            check(d.best_time == 1 && tmagic == 0x31545453u && tf > 0, "初クリアでベストタイムが SRAM に保存される");
            /* リセットすると計測値が消えるので、先に処理落ちを確認しておく */
            printf("  処理負荷 (リセット前): 最大 %u / 228 走査線, 処理落ち %u フレーム\n", d.max_lines, d.lag_frames);
            check(d.lag_frames == 0, "このステージで処理落ちが発生していない");
            /* リセット後のタイトルにステージの最高ランクが出る */
            core->reset(core);
            run(120, 0);
            screenshot("title_best_rank");
            if (st == 0) {
                /* もう一度同じステージをクリア: 同じタイムなので更新されず BEST が表示される */
                core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), st);
                core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 1);
                run(30, 0);
                wait_state(GS_PLAY, 400);
                DebugInfo p2;
                read_debug(&p2);
                int stuck2 = 0;
                uint32_t c0 = p2.clears;
                for (int t = 0; t < 60 * 120 && p2.clears == c0; t++) bot_step(t, &p2, &stuck2);
                run(60, 0);
                read_debug(&d);
                screenshot("stage_clear_best");
                uint32_t tf2 = core->busRead8(core, 0x0E000024) | (core->busRead8(core, 0x0E000025) << 8);
                printf("  2 回目: %s (記録 %u → %u フレーム)\n", d.best_time ? "ベスト更新" : "更新なし", tf, tf2);
                check(d.state == GS_CLEAR && tf2 <= tf && (d.best_time ? tf2 < tf : tf2 == tf),
                      "2 回目のクリアでベストタイムは速いときだけ更新される");
            }
        }
    } else if (strncmp(scenario, "boss", 4) == 0) {
        /* ボス戦へ直行し、ボスの出現・攻撃・被弾を検証 (boss=ステージ3, boss5=ステージ5) */
        int bst = scenario[4] ? atoi(scenario + 4) - 1 : 2;
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), bst);
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 1);
        run(30, 0);
        wait_state(GS_PLAY, 400);
        read_debug(&d);
        check(d.state == GS_PLAY && (int)d.stage == bst, "ボスのステージへ移動できる");
        int arena_x = d.level_w * 16 - 240;
        /* ボス直前のチェックポイントの少し手前へワープして歩いて入場 */
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd_arg), arena_x - 18 * 16);
        core->busWrite32(core, 0x02000000 + offsetof(DebugInfo, cmd), 2);
        for (int i = 0; i < 400; i++) {
            read_debug(&d);
            if (d.boss_hp > 0) break;
            run(1, KRIGHT);
        }
        read_debug(&d);
        check(d.boss_hp > 0, "闘技場でボスが出現する");
        screenshot("boss_01_intro");
        run(76, 0);
        screenshot("boss_01b_warning");
        run(44, 0);
        screenshot("boss_02_fight");
        int hp0 = d.boss_hp;
        /* ボット戦術でボスと戦う */
        DebugInfo prev = d;
        int stuck = 0;
        int min_hp = 99;
        int out_of_arena = 0;
        int finish_shot = 0;
        for (int t = 0; t < 5400; t++) {
            read_debug(&d);
            if (d.boss_hp > 0 && d.boss_hp < min_hp) min_hp = d.boss_hp;
            if (t == 600) screenshot("boss_03_mid");
            if (d.boss_state == BS_DEAD && d.state == GS_PLAY && !finish_shot) {
                finish_shot = 1;
                /* とどめの一撃 → スローモーション中の爆発 */
                run(20, 0);
                screenshot("boss_03b_finish");
                run(30, 0);
                screenshot("boss_03c_finish");
                read_debug(&d);
            }
            if (getenv("BOSS_LOG") && d.hp < prev.hp && d.state == GS_PLAY)
                printf("  [hurt] t=%d hp %d->%d boss_state=%u dx=%d py=%d bossmove=%d\n", t, prev.hp, d.hp, d.boss_state, d.boss_x - d.px - 5, d.py, d.boss_x - prev.boss_x);
            if (d.boss_hp > 0 && (d.boss_x < (int)d.cam_x || d.boss_x > (int)d.cam_x + 240)) out_of_arena++;
            if (d.state == GS_CLEAR || d.state == GS_ENDING) break;
            bot_step(t, &prev, &stuck);
        }
        (void)prev;
        read_debug(&d);
        print_debug("boss end");
        printf("  ボス残りHP: %d (最小 %d / 開始 %d)\n", d.boss_hp, min_hp, hp0);
        check(out_of_arena == 0, "ボスが闘技場の外に出ない");
        check(min_hp < hp0 || d.state == GS_CLEAR || d.state == GS_ENDING, "ボスにダメージを与えられる");
        check(d.state == GS_CLEAR || d.state == GS_ENDING, "ボット戦術でボスを撃破できる (攻略可能性の検証)");
        if (bst == 4 && (d.state == GS_CLEAR || d.state == GS_ENDING)) {
            /* 最終ボス撃破 → エンディング → ハイスコアが SRAM に保存される */
            for (int i = 0; i < 400 && d.state != GS_ENDING; i++) {
                run(1, i % 30 == 0 ? KSTART : 0);
                read_debug(&d);
            }
            run(30, 0);
            check(d.state == GS_ENDING, "最終ボス撃破後にエンディングへ進む");
            screenshot("boss_05_ending");
            uint32_t magic = 0, hs = 0;
            for (int i = 0; i < 4; i++) {
                magic |= (uint32_t)core->busRead8(core, 0x0E000000 + i) << (i * 8);
                hs |= (uint32_t)core->busRead8(core, 0x0E000004 + i) << (i * 8);
            }
            printf("  SRAM: magic=%08X hiscore=%u\n", magic, hs);
            check(magic == 0x31435453u && hs == d.score, "ハイスコアが SRAM に保存される");
            uint32_t reached = core->busRead8(core, 0x0E00000D);
            printf("  SRAM: 到達ステージ=%u\n", reached + 1);
            check(reached == 4, "到達ステージ (ステージセレクト用) が SRAM に保存される");
            /* スタッフロール: START で開始 → A 長押しで早送り → THE END → START でタイトル */
            run(130, 0);
            run(2, KSTART);
            run(300, 0);
            read_debug(&d);
            check(d.state == GS_ENDING && d.ending_phase == 1, "リザルトの後にスタッフロールが流れる");
            screenshot("boss_05b_staffroll");
            run(240, KA);
            screenshot("boss_05c_staffroll_cast");
            run(160, KA);
            screenshot("boss_05c2_staffroll_boss");
            for (int i = 0; i < 2000 && d.ending_phase != 2; i++) { run(1, KA); read_debug(&d); }
            check(d.ending_phase == 2, "スタッフロールが最後まで流れて THE END になる");
            run(80, 0);
            screenshot("boss_05d_the_end");
            run(2, KSTART);
            run(5, 0);
            read_debug(&d);
            check(d.state == GS_TITLE, "THE END から START でタイトルに戻る");
            /* リセット後のタイトルに記録が表示される */
            core->reset(core);
            run(120, 0);
            screenshot("boss_06_title_hiscore");
            /* ステージセレクト: 上で 5 → 4 ... 上キーで STAGE 2 を選んで開始 */
            run(2, KUP);
            run(4, 0);
            run(2, KSTART);
            run(30, 0);
            wait_state(GS_PLAY, 400);
            read_debug(&d);
            check(d.stage == 1, "タイトルのステージセレクトで STAGE 2 から開始できる");
        }
        screenshot("boss_04_end");
    } else {
        DebugInfo prev = d;
        int stuck = 0;
        uint32_t max_stage = 0;
        int popup_shot = 0;
        for (int t = 0; t < 60 * 60 * 6; t++) {
            bot_step(t, &prev, &stuck);
            if (prev.stage > max_stage) max_stage = prev.stage;
            if (prev.popups == 1 && !popup_shot) {
                run(10, 0);
                screenshot("bot_chain_popup");
                popup_shot = 1;
            }
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
        read_debug(&d);
        printf("  ステージ別ランク:");
        for (int i = 0; i < 5; i++) {
            uint32_t r = (d.rank_hist >> (i * 4)) & 15;
            printf(" %c", r ? "CBAS"[r - 1] : '-');
        }
        printf("\n");
        read_debug(&d);
        check(d.clears >= 1, "ボットがステージ 1 をクリアできる (難易度/地形の回帰検知)");
        if (!strstr(scenario, "hard")) check(d.popups >= 1, "CHAIN の節目 (10) でポップアップとボーナスが出る");
    }

    print_debug("final");
    read_debug(&d);
    printf("  処理負荷: 最大 %u / 228 走査線 (state=%u t=%u), 処理落ち %u フレーム (最後: state=%u t=%u)\n", d.max_lines,
           d.max_state & 0xFF, d.max_state >> 8, d.lag_frames, d.lag_state & 0xFF, d.lag_state >> 8);
    check(d.lag_frames == 0, "処理落ち (1 フレームに収まらない) が発生していない");
    printf("== %s: %d failure(s), %u frames ==\n", failures ? "FAILED" : "PASSED", failures, frames_run);
    core->deinit(core);
    return failures ? 1 : 0;
}
