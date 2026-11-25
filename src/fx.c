/* エフェクト (爆発・火花・土煙・破片) と画面フラッシュ */
#include "game.h"

typedef struct {
    u8 active, type, flip;
    u8 t;
    s32 x, y;   /* 固定小数 */
    s16 vx, vy;
} Fx;
#define MAX_FX 32
static Fx fxs[MAX_FX];
static u8 flash_t;

void fx_clear(void) {
    memset(fxs, 0, sizeof(fxs));
    flash_t = 0;
}

static Fx* fx_alloc(void) {
    for (int i = 0; i < MAX_FX; i++)
        if (!fxs[i].active) return &fxs[i];
    return 0;
}

void fx_spawn(int type, int x, int y, int flip) {
    Fx* f = fx_alloc();
    if (!f) return;
    f->active = 1;
    f->type = (u8)type;
    f->flip = (u8)flip;
    f->t = 0;
    f->x = TO_FP(x);
    f->y = TO_FP(y);
    f->vx = f->vy = 0;
}

void fx_spawn_slash(int spr0, int x, int y, int flip) { (void)spr0; fx_spawn(FX_SLASH, x, y, flip); }

/* 破片を放射状に飛ばす */
void fx_debris(int x, int y, int n) {
    for (int i = 0; i < n; i++) {
        Fx* f = fx_alloc();
        if (!f) return;
        f->active = 1;
        f->type = FX_DEBRIS;
        f->t = (u8)rng_range(6);
        f->flip = (u8)(i & 1);
        f->x = TO_FP(x - 4);
        f->y = TO_FP(y - 4);
        f->vx = (s16)(rng_range(0x400) - 0x200);
        f->vy = (s16)(-0x180 - rng_range(0x280));
    }
}

/* 画面を一瞬白くする (BLDY による輝度上昇) */
void screen_flash(int frames) {
    if (frames > flash_t) flash_t = (u8)frames;
}

void fx_update(void) {
    for (int i = 0; i < MAX_FX; i++) {
        Fx* f = &fxs[i];
        if (!f->active) continue;
        f->t++;
        int life;
        switch (f->type) {
        case FX_BOOM: life = 20; break;
        case FX_SPARK: life = 8; break;
        case FX_DEBRIS: life = 50; break;
        default: life = 14; break;
        }
        if (f->type == FX_DUST && (f->t & 3) == 0) f->y -= TO_FP(1);
        if (f->type == FX_DEBRIS) {
            f->vy += 0x30;
            f->x += f->vx;
            f->y += f->vy;
            /* 床で跳ねる */
            int px = FROM_FP(f->x) + 4, py = FROM_FP(f->y) + 6;
            if (f->vy > 0 && level_solid(px >> 4, py >> 4)) {
                f->y = TO_FP(((py >> 4) << 4) - 6);
                f->vy = (s16)(-f->vy / 2);
                f->vx = (s16)(f->vx / 2);
            }
        }
        if (f->t >= life) f->active = 0;
    }
}

/* VBlank 中に呼ぶ: フラッシュの反映 */
void fx_flash_apply(void) {
    if (flash_t) {
        REG_BLDCNT = 0x00BF;  /* 全レイヤー + 背景色を輝度上昇 */
        REG_BLDY = (u16)MIN(16, flash_t * 4);
        flash_t--;
    } else {
        /* 半透明スプライト (残像) 用: 第2対象を BG1/BG2/背景色に */
        REG_BLDCNT = 0x2640;
        REG_BLDALPHA = 0x0A08;
        REG_BLDY = 0;
    }
}

void fx_draw(void) {
    for (int i = 0; i < MAX_FX; i++) {
        Fx* f = &fxs[i];
        if (!f->active) continue;
        int sx = FROM_FP(f->x) - cam_x, sy = FROM_FP(f->y) - cam_y;
        switch (f->type) {
        case FX_BOOM: oam_put(SPR_BOOM0 + MIN(3, f->t / 5), sx, sy, f->flip, OBJPAL_NORMAL, 1); break;
        case FX_SPARK: oam_put(SPR_SPARK0 + (f->t >= 4), sx, sy, f->flip, OBJPAL_NORMAL, 0); break;
        case FX_DUST: oam_put(SPR_DUST0 + (f->t >= 7), sx + (f->flip ? f->t / 3 : -f->t / 3), sy, 0, OBJPAL_NORMAL, 1); break;
        case FX_DEBRIS:
            if (f->t > 40 && (f->t & 2)) break;
            oam_put(SPR_DEBRIS0 + ((f->t >> 2) & 1), sx, sy, f->flip, OBJPAL_NORMAL, 1);
            break;
        }
    }
}
