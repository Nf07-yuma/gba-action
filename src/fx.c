/* エフェクト (爆発・火花・土煙) */
#include "game.h"

typedef struct {
    u8 active, type, flip;
    u8 t;
    s16 x, y;
} Fx;
#define MAX_FX 24
static Fx fxs[MAX_FX];

void fx_clear(void) { memset(fxs, 0, sizeof(fxs)); }

void fx_spawn(int type, int x, int y, int flip) {
    Fx* f = 0;
    for (int i = 0; i < MAX_FX; i++)
        if (!fxs[i].active) {
            f = &fxs[i];
            break;
        }
    if (!f) return;
    f->active = 1;
    f->type = (u8)type;
    f->flip = (u8)flip;
    f->t = 0;
    f->x = (s16)x;
    f->y = (s16)y;
}

void fx_spawn_slash(int spr0, int x, int y, int flip) { (void)spr0; fx_spawn(FX_SLASH, x, y, flip); }

void fx_update(void) {
    for (int i = 0; i < MAX_FX; i++) {
        Fx* f = &fxs[i];
        if (!f->active) continue;
        f->t++;
        int life = f->type == FX_BOOM ? 20 : f->type == FX_SPARK ? 8 : 14;
        if (f->type == FX_DUST && (f->t & 3) == 0) f->y--;
        if (f->t >= life) f->active = 0;
    }
}

void fx_draw(void) {
    for (int i = 0; i < MAX_FX; i++) {
        Fx* f = &fxs[i];
        if (!f->active) continue;
        int sx = f->x - cam_x, sy = f->y - cam_y;
        switch (f->type) {
        case FX_BOOM: oam_put(SPR_BOOM0 + MIN(3, f->t / 5), sx, sy, f->flip, OBJPAL_NORMAL, 1); break;
        case FX_SPARK: oam_put(SPR_SPARK0 + (f->t >= 4), sx, sy, f->flip, OBJPAL_NORMAL, 0); break;
        case FX_DUST: oam_put(SPR_DUST0 + (f->t >= 7), sx + (f->flip ? f->t / 3 : -f->t / 3), sy, 0, OBJPAL_NORMAL, 1); break;
        }
    }
}
