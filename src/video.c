/* 描画基盤: OAM シャドウ / スプライト VRAM / テキスト */
#include "game.h"

static OamEntry oam_buf[128] ALIGN4;
static int oam_count;
u16 sprite_tile_base[SPR_COUNT];
/* 大型スプライト (ボス) は常駐させず、表示中のフレームだけを専用枠へ転送する */
static u16 dyn_base;
static s16 dyn_loaded = -1, dyn_pending = -1;

static int is_dynamic(int i) {
    return (i >= SPR_BOSS_IDLE0 && i <= SPR_BOSS_SHOOT) || (i >= SPR_CORE_CLOSED && i <= SPR_CORE_GLOW);
}

/* ボスの描画前に呼ぶ: 次の VBlank で転送する */
void sprite_request_dynamic(int spr) {
    if (spr != dyn_loaded) dyn_pending = (s16)spr;
}

/* VBlank 中に呼ぶ */
void sprite_flush_dynamic(void) {
    if (dyn_pending < 0) return;
    const SpriteFrame* f = &sprite_frames[dyn_pending];
    dma3_copy32(OBJ_TILES + dyn_base * 16, f->data, f->tiles * 8);
    dyn_loaded = dyn_pending;
    dyn_pending = -1;
}

void vsync(void) { bios_vblank_wait(); }

void video_init(void) {
    REG_DISPCNT = DCNT_BLANK;
    /* パレット */
    dma3_copy32(PAL_OBJ, pal_obj, sizeof(pal_obj) / 4);
    dma3_copy32(PAL_BG, pal_bg, sizeof(pal_bg) / 4);
    /* BG タイル */
    dma3_copy32(CHARBLOCK(CBB_LEVEL), level_tiles, sizeof(level_tiles) / 4);
    dma3_copy32(CHARBLOCK(CBB_UI), ui_tiles, sizeof(ui_tiles) / 4);
    dma3_copy32(CHARBLOCK(CBB_SKY), sky_tiles, sizeof(sky_tiles) / 4);
    dma3_copy32(CHARBLOCK(CBB_MID), mid_tiles, sizeof(mid_tiles) / 4);
    /* スプライトタイルを全て常駐ロード */
    u32 t = 0;
    for (int i = 0; i < SPR_COUNT; i++) {
        const SpriteFrame* f = &sprite_frames[i];
        if (is_dynamic(i)) continue;
        sprite_tile_base[i] = t;
        dma3_copy32(OBJ_TILES + t * 16, f->data, f->tiles * 8);
        t += f->tiles;
    }
    /* 動的枠 (64x64 = 64 タイル) */
    dyn_base = (u16)t;
    for (int i = 0; i < SPR_COUNT; i++)
        if (is_dynamic(i)) sprite_tile_base[i] = dyn_base;
    dyn_loaded = dyn_pending = -1;
    g_debug.obj_tiles = t + 64;
    /* マップ初期化 */
    dma3_fill32(SCREENBLOCK(SBB_LEVEL), 0, 2 * 0x800 / 4);
    dma3_fill32(SCREENBLOCK(SBB_HUD), 0, 0x800 / 4);
    dma3_copy32(SCREENBLOCK(SBB_SKY), sky_map, sizeof(sky_map) / 4);
    dma3_copy32(SCREENBLOCK(SBB_MID), mid_map, sizeof(mid_map) / 4);
    dma3_copy32(PAL_BG + BGPAL_MID * 16, theme_mid_pals, 8);

    REG_BGCNT(BG_HUD) = BG_PRIO(0) | BG_CBB(CBB_UI) | BG_SBB(SBB_HUD) | BG_SIZE_32x32;
    REG_BGCNT(BG_LEVEL) = BG_PRIO(2) | BG_CBB(CBB_LEVEL) | BG_SBB(SBB_LEVEL) | BG_SIZE_64x32;
    REG_BGCNT(BG_SKY) = BG_PRIO(3) | BG_CBB(CBB_SKY) | BG_SBB(SBB_SKY) | BG_SIZE_32x32;
    REG_BGCNT(BG_MID) = BG_PRIO(3) | BG_CBB(CBB_MID) | BG_SBB(SBB_MID) | BG_SIZE_32x32;
    for (int i = 0; i < 4; i++) {
        REG_BGHOFS(i) = 0;
        REG_BGVOFS(i) = 0;
    }
    for (int i = 0; i < 128; i++) {
        oam_buf[i].attr0 = ATTR0_HIDE;
        oam_buf[i].attr1 = 0;
        oam_buf[i].attr2 = 0;
    }
    dma3_copy32(MEM_OAM, oam_buf, sizeof(oam_buf) / 4);
    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ;
}

void oam_begin(void) { oam_count = 0; }

void oam_end(void) {
    for (int i = oam_count; i < 128; i++) oam_buf[i].attr0 = ATTR0_HIDE;
    /* VBlank 直後に呼ばれる前提 */
    for (int i = 0; i < 128; i++) {
        MEM_OAM[i * 4 + 0] = oam_buf[i].attr0;
        MEM_OAM[i * 4 + 1] = oam_buf[i].attr1;
        MEM_OAM[i * 4 + 2] = oam_buf[i].attr2;
    }
}

void oam_put(int spr, int x, int y, int flip, int pal, int prio) {
    if (oam_count >= 128) return;
    const SpriteFrame* f = &sprite_frames[spr];
    int w = f->w, h = f->h;
    if (x <= -w || x >= SCREEN_W || y <= -h || y >= SCREEN_H) return;
    u16 shape, size;
    if (w == h) {
        shape = ATTR0_SQUARE;
        size = (w == 8) ? 0 : (w == 16) ? 1 : (w == 32) ? 2 : 3;
    } else if (w > h) {
        shape = ATTR0_WIDE;
        size = (w == 16) ? 0 : (h == 8) ? 1 : (h == 16) ? 2 : 3;
    } else {
        shape = ATTR0_TALL;
        size = (h == 16) ? 0 : (w == 8) ? 1 : (w == 16) ? 2 : 3;
    }
    OamEntry* o = &oam_buf[oam_count++];
    o->attr0 = ATTR0_Y(y) | shape;
    o->attr1 = ATTR1_X(x) | ATTR1_SIZE(size) | (flip ? ATTR1_HFLIP : 0);
    o->attr2 = ATTR2_TILE(sprite_tile_base[spr]) | ATTR2_PRIO(prio) | ATTR2_PAL(pal);
}

/* 半透明で描画 (BLDCNT の第2対象とアルファ合成) */
void oam_put_blend(int spr, int x, int y, int flip, int pal, int prio) {
    int before = oam_count;
    oam_put(spr, x, y, flip, pal, prio);
    if (oam_count > before) oam_buf[before].attr0 |= ATTR0_BLEND;
}

/* ---- テキスト (BG0, 32x32 マップ) ---- */
void text_clear(void) { dma3_fill32(SCREENBLOCK(SBB_HUD), 0, 0x800 / 4); }

void text_put(int x, int y, const char* s, int pal) {
    u16* m = SCREENBLOCK(SBB_HUD);
    for (; *s && x < 32; s++, x++) {
        int c = (u8)*s;
        if (c < FONT_FIRST || c > FONT_LAST) c = ' ';
        if (x >= 0) m[y * 32 + x] = (u16)((FONT_TILE_BASE + c - FONT_FIRST) | (pal << 12));
    }
}

void text_put_center(int y, const char* s, int pal) {
    int n = 0;
    while (s[n]) n++;
    text_put((30 - n) / 2, y, s, pal);
}

void text_num(int x, int y, u32 n, int digits, int pal) {
    char buf[12];
    buf[digits] = 0;
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = (char)('0' + n % 10);
        n /= 10;
    }
    text_put(x, y, buf, pal);
}

void text_fill(int x, int y, int w, int h, int tile, int pal) {
    u16* m = SCREENBLOCK(SBB_HUD);
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) m[j * 32 + i] = (u16)(tile | (pal << 12));
}

/* mm:ss 形式で時間を表示 */
void text_time(int x, int y, u32 frames, int pal) {
    u32 sec = frames / 60;
    text_num(x, y, MIN(99u, sec / 60), 2, pal);
    text_put(x + 2, y, ":", pal);
    text_num(x + 3, y, sec % 60, 2, pal);
}
