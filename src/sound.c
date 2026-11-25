/* 効果音 (PSG) */
#include "game.h"

static u8 noise_busy, sq_busy;

void sound_init(void) {
    REG_SOUNDCNT_X = 0x0080;              /* マスター ON */
    REG_SOUNDCNT_L = 0xFF77;              /* 全 PSG ch を L/R, 音量最大 */
    REG_SOUNDCNT_H = 0x0002;              /* PSG 100% */
    REG_SOUNDBIAS = 0x0200;
}

/* ch1: スイープ付き矩形波 */
static void sq1(u16 sweep, u16 duty_env, u16 freq) {
    REG_SOUND1CNT_L = sweep;
    REG_SOUND1CNT_H = duty_env;
    REG_SOUND1CNT_X = (u16)(0x8000 | (freq & 0x7FF));
}
/* ch2: 矩形波 */
static void sq2(u16 duty_env, u16 freq) {
    REG_SOUND2CNT_L = duty_env;
    REG_SOUND2CNT_H = (u16)(0x8000 | (freq & 0x7FF));
}
/* ch4: ノイズ */
static void noise(u16 env, u16 poly) {
    REG_SOUND4CNT_L = env;
    REG_SOUND4CNT_H = (u16)(0x8000 | poly);
}

void sfx_play(int id) {
    switch (id) {
    case SFX_JUMP:   sq1(0x0027, 0xA180, 1600); sq_busy = 6; break;
    case SFX_SLASH:  noise(0x7100 | 0x0000, 0x0012); break;
    case SFX_SLASH3: noise(0xB200, 0x0022); sq1(0x002F, 0x8280, 1500); break;
    case SFX_HIT:    noise(0xC100, 0x0031); sq2(0x8180, 1850); break;
    case SFX_BOOM:   noise(0xF300, 0x0056); noise_busy = 20; break;
    case SFX_HURT:   sq1(0x007B, 0xF280, 1700); break;
    case SFX_PICKUP: sq2(0xB180, 1900); sq1(0x0016, 0xA180, 1800); break;
    case SFX_DASH:   noise(0x8100, 0x0004); break;
    case SFX_SHOT:   sq1(0x001E, 0x6180, 1750); break;
    case SFX_SELECT: sq2(0xA180, 1950); break;
    case SFX_CHECK:  sq1(0x0014, 0xC280, 1880); sq2(0x8280, 1960); break;
    }
}

void sound_update(void) {
    if (noise_busy) noise_busy--;
    if (sq_busy) sq_busy--;
}
