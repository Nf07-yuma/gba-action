/* ステージ: セル参照・当たり判定・マップのストリーミング・カメラ */
#include "game.h"

Level level;
s32 cam_x, cam_y;
int shake_t, shake_amp;
static u16 col_loaded[64]; /* BG マップ列 -> 読み込み済みのワールドタイル列 (+1, 0=未) */

int level_cell(int cx, int cy) {
    if (cx < 0 || cx >= level.w) return CELL_SOLID;
    if (cy < 0 || cy >= level.h) return CELL_EMPTY;
    return level.def->cells[cy * level.w + cx] & 0x0F;
}

int level_solid(int cx, int cy) { return level_cell(cx, cy) == CELL_SOLID; }

static int metatile_at(int cx, int cy) {
    if (cx < 0 || cx >= level.w || cy < 0 || cy >= level.h) return MT_EMPTY;
    u8 raw = level.def->cells[cy * level.w + cx];
    int c = raw & 0x0F;
    switch (c) {
    case CELL_SOLID: {
        int up = level_solid(cx, cy - 1) || cy == 0;
        int l = cx == 0 || level_solid(cx - 1, cy);
        int r = cx == level.w - 1 || level_solid(cx + 1, cy);
        if (!up) {
            if (!l && !r) return MT_STEEL_TOP_LR;
            if (!l) return MT_STEEL_TOP_L;
            if (!r) return MT_STEEL_TOP_R;
            return MT_STEEL_TOP;
        }
        if (!l) return MT_STEEL_L;
        if (!r) return MT_STEEL_R;
        u32 hsh = (u32)(cx * 7 + cy * 13 + (cx >> 2) * 5);
        hsh %= 11;
        return hsh == 0 ? MT_STEEL_B : (hsh == 5 ? MT_STEEL_C : MT_STEEL);
    }
    case CELL_ONEWAY: return MT_GIRDER;
    case CELL_HAZARD: return MT_SPIKES;
    case CELL_BACK: {
        int k = raw >> 4;
        return k == 0 ? MT_BACK_PIPE : k == 1 ? MT_BACK_WINDOW : k == 2 ? MT_BACK_GRID : MT_BACK_PLAIN;
    }
    default: return MT_EMPTY;
    }
}

static void write_column(int tx) {
    int mc = tx & 63;
    u16* sb = SCREENBLOCK(SBB_LEVEL + (mc >> 5));
    int col = mc & 31;
    int cx = tx >> 1;
    for (int ty = 0; ty < 32; ty++) {
        int mt = metatile_at(cx, ty >> 1);
        sb[ty * 32 + col] = metatiles[mt * 4 + (ty & 1) * 2 + (tx & 1)];
    }
    col_loaded[mc] = (u16)(tx + 1);
}

void level_scroll_update(void) {
    int t0 = (cam_x >> 3) - 2;
    int t1 = t0 + 34;
    for (int tx = t0; tx <= t1; tx++) {
        if (tx < 0 || tx >= level.w * 2) continue;
        if (col_loaded[tx & 63] != tx + 1) write_column(tx);
    }
    int sx = cam_x, sy = cam_y;
    if (shake_t > 0) {
        shake_t--;
        sx += (rng_range(shake_amp * 2 + 1) - shake_amp);
        sy += (rng_range(shake_amp * 2 + 1) - shake_amp);
    }
    REG_BGHOFS(BG_LEVEL) = (u16)sx;
    REG_BGVOFS(BG_LEVEL) = (u16)sy;
    /* 遠景は 1/4 速度で横スクロール, 縦は少しだけ */
    REG_BGHOFS(BG_SKY) = (u16)(cam_x >> 2);
    REG_BGVOFS(BG_SKY) = (u16)(96 + (cam_y >> 3));
}

void level_force_redraw(void) {
    for (int i = 0; i < 64; i++) col_loaded[i] = 0;
    level_scroll_update();
}

void level_load(int index) {
    level.index = index;
    level.def = &levels[index];
    level.w = level.def->w;
    level.h = level.def->h;
    level.px_w = level.w * CELL;
    level.px_h = level.h * CELL;
    dma3_fill32(SCREENBLOCK(SBB_LEVEL), 0, 2 * 0x800 / 4);
    for (int i = 0; i < 64; i++) col_loaded[i] = 0;
    shake_t = 0;
}

/* ---- 当たり判定 ---- */
static int col_any(int cx, int cy0, int cy1, int type) {
    for (int cy = cy0; cy <= cy1; cy++)
        if (level_cell(cx, cy) == type) return 1;
    return 0;
}
static int row_any(int cy, int cx0, int cx1, int type) {
    for (int cx = cx0; cx <= cx1; cx++)
        if (level_cell(cx, cy) == type) return 1;
    return 0;
}

void body_move(Body* b) {
    b->hit_wall = 0;
    /* X 移動 */
    s32 nx = b->x + b->vx;
    int top = FROM_FP(b->y), bot = FROM_FP(b->y) + b->h - 1;
    if (b->vx > 0) {
        int right = FROM_FP(nx) + b->w - 1;
        if (col_any(right >> 4, top >> 4, bot >> 4, CELL_SOLID)) {
            nx = TO_FP(((right >> 4) << 4) - b->w);
            b->vx = 0;
            b->hit_wall = 1;
        }
    } else if (b->vx < 0) {
        int left = FROM_FP(nx);
        if (col_any(left >> 4, top >> 4, bot >> 4, CELL_SOLID)) {
            nx = TO_FP(((left >> 4) + 1) << 4);
            b->vx = 0;
            b->hit_wall = 1;
        }
    }
    b->x = nx;
    /* Y 移動 */
    s32 ny = b->y + b->vy;
    int l = FROM_FP(b->x), r = l + b->w - 1;
    b->on_ground = 0;
    if (b->vy > 0) {
        int oldbot = FROM_FP(b->y) + b->h - 1;
        int nb = FROM_FP(ny) + b->h - 1;
        int cy = nb >> 4;
        int land = row_any(cy, l >> 4, r >> 4, CELL_SOLID) || row_any(cy, l >> 4, r >> 4, CELL_HAZARD);
        if (!land && b->oneway && !b->drop && (oldbot >> 4) < cy && row_any(cy, l >> 4, r >> 4, CELL_ONEWAY))
            land = 1;
        if (land) {
            ny = TO_FP((cy << 4) - b->h);
            b->vy = 0;
            b->on_ground = 1;
        }
    } else if (b->vy < 0) {
        int nt = FROM_FP(ny);
        if (row_any(nt >> 4, l >> 4, r >> 4, CELL_SOLID)) {
            ny = TO_FP(((nt >> 4) + 1) << 4);
            b->vy = 0;
        }
    }
    b->y = ny;
    /* 一方通行すり抜けの解除: 足場セルを抜けたら戻す */
    if (b->drop) {
        int fb = FROM_FP(b->y) + b->h - 1;
        if (!row_any(fb >> 4, l >> 4, r >> 4, CELL_ONEWAY) && !row_any((fb >> 4) - 1, l >> 4, r >> 4, CELL_ONEWAY))
            b->drop = 0;
    }
}

int body_touch_hazard(const Body* b) {
    /* トゲは床として乗れるが、乗る(足元が接する)とダメージ */
    int l = FROM_FP(b->x) + 2, r = FROM_FP(b->x) + b->w - 3;
    int bot = FROM_FP(b->y) + b->h;
    return row_any(bot >> 4, l >> 4, r >> 4, CELL_HAZARD) || row_any((bot - 1) >> 4, l >> 4, r >> 4, CELL_HAZARD);
}

/* ---- カメラ ---- */
static s32 look_ahead;

static void camera_clamp(void) {
    int maxx = level.px_w - SCREEN_W;
    int maxy = level.px_h - SCREEN_H;
    if (maxy < 0) maxy = 0;
    cam_x = CLAMP(cam_x, 0, maxx);
    if (boss_lock_x >= 0 && cam_x < boss_lock_x) cam_x = boss_lock_x;
    cam_y = CLAMP(cam_y, 0, maxy);
}

void camera_snap(s32 tx, s32 ty) {
    look_ahead = 0;
    cam_x = tx - SCREEN_W / 2;
    cam_y = ty - SCREEN_H / 2 - 8;
    camera_clamp();
}

void camera_update(s32 tx, s32 ty, int dir) {
    /* 進行方向に少し先読み */
    s32 want = dir * 32;
    look_ahead += (want - look_ahead) / 16;
    s32 gx = tx + look_ahead - SCREEN_W / 2;
    s32 gy = ty - SCREEN_H / 2 - 8;
    cam_x += (gx - cam_x) / 6;
    s32 dy = gy - cam_y;
    if (dy > 24 || dy < -24) cam_y += dy / 4;
    else cam_y += dy / 10;
    camera_clamp();
}
