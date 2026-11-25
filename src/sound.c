/* サウンド: 効果音 (ch1/ch4) と BGM シーケンサ (ch2=メロディ, ch3=ベース, ch4=ドラム) */
#include "game.h"

static u8 noise_busy;

/* ---- BGM 状態 ---- */
static const Song* song;
static u16 pos_lead, pos_bass, pos_drum;
static u8 tick;
static u8 paused;

/* ベース用波形 (32 サンプル, 4bit): 丸みのあるノコギリ波 */
static const u32 bass_wave[4] = {0x89ABCDEF, 0xFEDCBA98, 0x77665544, 0x33221100};

void sound_init(void) {
    REG_SOUNDCNT_X = 0x0080;              /* マスター ON */
    REG_SOUNDCNT_L = 0xFF77;              /* 全 PSG ch を L/R, 音量最大 */
    REG_SOUNDCNT_H = 0x0002;              /* PSG 100% */
    REG_SOUNDBIAS = 0x0200;
    /* 波形メモリ書き込み (bank1 を再生中にして bank0 を書く) */
    REG_SOUND3CNT_L = 0x0040;
    for (int i = 0; i < 4; i++) WAVE_RAM[i] = bass_wave[i];
    REG_SOUND3CNT_L = 0x0000;
}

/* ch1: スイープ付き矩形波 */
static void sq1(u16 sweep, u16 duty_env, u16 freq) {
    REG_SOUND1CNT_L = sweep;
    REG_SOUND1CNT_H = duty_env;
    REG_SOUND1CNT_X = (u16)(0x8000 | (freq & 0x7FF));
}
/* ch4: ノイズ */
static void noise(u16 env, u16 poly) {
    REG_SOUND4CNT_L = env;
    REG_SOUND4CNT_H = (u16)(0x8000 | poly);
}

void sfx_play(int id) {
    switch (id) {
    case SFX_JUMP:   sq1(0x0027, 0xA180, 1600); break;
    case SFX_SLASH:  noise(0x7100, 0x0012); noise_busy = 6; break;
    case SFX_SLASH3: noise(0xB200, 0x0022); sq1(0x002F, 0x8280, 1500); noise_busy = 12; break;
    case SFX_HIT:    noise(0xC100, 0x0031); sq1(0x0000, 0x8180, 1850); noise_busy = 8; break;
    case SFX_BOOM:   noise(0xF300, 0x0056); noise_busy = 24; break;
    case SFX_HURT:   sq1(0x007B, 0xF280, 1700); break;
    case SFX_PICKUP: sq1(0x0016, 0xB180, 1800); break;
    case SFX_DASH:   noise(0x8100, 0x0004); noise_busy = 6; break;
    case SFX_SHOT:   sq1(0x001E, 0x6180, 1750); break;
    case SFX_SELECT: sq1(0x0000, 0xA180, 1950); break;
    case SFX_CHECK:  sq1(0x0014, 0xC280, 1880); break;
    }
}

/* ---- BGM ---- */
void music_play(int id) {
    const Song* s = &songs[id];
    if (song == s && !paused) return;
    song = s;
    pos_lead = pos_bass = pos_drum = 0;
    tick = 0;
    paused = 0;
}

void music_stop(void) {
    song = 0;
    REG_SOUND2CNT_L = 0;
    REG_SOUND2CNT_H = 0x8000;
    REG_SOUND3CNT_L = 0;
}

void music_pause(int on) {
    paused = (u8)on;
    if (on) {
        REG_SOUND2CNT_L = 0;
        REG_SOUND2CNT_H = 0x8000;
        REG_SOUND3CNT_L = 0;
    }
}

static void play_step(void) {
    u8 n = song->lead[pos_lead];
    if (n == MUS_OFF) {
        REG_SOUND2CNT_L = 0;
        REG_SOUND2CNT_H = 0x8000;
    } else if (n != MUS_HOLD) {
        REG_SOUND2CNT_L = 0x8440;  /* 音量8, 減衰 step4, duty 25% */
        REG_SOUND2CNT_H = (u16)(0x8000 | freq_square[n]);
    }
    u8 b = song->bass[pos_bass];
    if (b == MUS_OFF) {
        REG_SOUND3CNT_L = 0;
    } else if (b != MUS_HOLD) {
        REG_SOUND3CNT_L = 0x0080;
        REG_SOUND3CNT_H = 0x2000;  /* 音量 100% */
        REG_SOUND3CNT_X = (u16)(0x8000 | freq_wave[b]);
    }
    u8 d = song->drum[pos_drum];
    if (d && !noise_busy) {
        switch (d) {
        case 1: noise(0xA100, 0x0074); break; /* キック */
        case 2: noise(0x9200, 0x0032); break; /* スネア */
        case 3: noise(0x4100, 0x0008); break; /* ハイハット */
        }
    }
    if (!song->loop && pos_lead + 1 >= song->lead_len) {
        /* ジングルは最後まで鳴らしたら止める */
        song = 0;
        return;
    }
    if (++pos_lead >= song->lead_len) pos_lead = 0;
    if (++pos_bass >= song->bass_len) pos_bass = 0;
    if (++pos_drum >= song->drum_len) pos_drum = 0;
}

void sound_update(void) {
    if (noise_busy) noise_busy--;
    if (!song || paused) return;
    if (tick == 0) play_step();
    if (!song) return;
    if (++tick >= song->speed) tick = 0;
}
