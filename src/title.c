/* タイトル画面とデモプレイ (アトラクトモード) */
#include "game.h"

static int title_cursor;

void title_enter(void) {
    game.demo = 0;
    game_set_state(GS_TITLE);
    REG_BGVOFS(BG_HUD) = 0;
    bg_dim = 0;
    dma3_copy32(PAL_BG, &theme_pals[0], 16);
    dma3_copy32(PAL_BG + BGPAL_MID * 16, &theme_mid_pals[0], 8);
    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ;
    text_clear();
    cam_x = 0;
    cam_y = 0;
    title_cursor = 0;
    music_play(SONG_TITLE);
}

/* ---- デモプレイ (アトラクトモード) ---- */
#define DEMO_IDLE 1200  /* タイトルで 20 秒放置で開始 */
#define DEMO_LEN  1500  /* 25 秒で終了 */
static u16 demo_jump;
static u16 demo_prev_ai;

/* デモ用の簡易 AI: 右へ走って斬り、穴や壁の手前で跳ぶ */
static u16 demo_keys(void) {
    u16 k = KEY_RIGHT;
    if ((game.frame % 14) < 2) k |= KEY_B;
    int feet = FROM_FP(player.b.y) + player.b.h;
    int fx = FROM_FP(player.b.x) + player.b.w + 4;
    if (player.b.on_ground && !demo_jump) {
        int below = level_cell(fx >> 4, (feet + 2) >> 4);
        int wall = level_solid((fx + 2) >> 4, (feet - 4) >> 4) || level_solid((fx + 2) >> 4, (feet - 20) >> 4);
        if ((below != CELL_SOLID && below != CELL_ONEWAY) || wall) demo_jump = 22;
    }
    if (demo_jump) {
        demo_jump--;
        if (demo_jump < 21) k |= KEY_A;
    }
    return k;
}

static void demo_start(void) {
    game.demo = 1;
    game.stage = 0;
    game.lives = 1;
    game.score = 0;
    game.easy = 0;
    game.hard = 0;
    demo_jump = 0;
    game_set_state(GS_STAGE_INTRO);
}

/* デモ中の入力: 実際のボタンが押されたらデモを終えてタイトルへ。それ以外は AI の入力に置き換え */
void demo_input(void) {
    static u16 demo_prev;
    u16 real = key_cur;
    if (real & ~demo_prev & KEY_MASK) {
        key_cur = key_prev = 0;
        title_enter();
    } else if (game.state == GS_PLAY && game.state_t > DEMO_LEN) {
        title_enter();
    } else if ((game.state == GS_DEAD && game.state_t > 60) || game.state == GS_GAMEOVER || game.state == GS_CLEAR) {
        title_enter();
    } else {
        key_prev = demo_prev_ai;
        key_cur = demo_keys();
        demo_prev_ai = key_cur;
    }
    demo_prev = real;
    if (game.demo && (game.frame & 32)) text_put_center(2, "DEMO PLAY", BGPAL_HUD_Y);
    else if (game.demo) text_put_center(2, "         ", BGPAL_HUD);
}

void title_update(void) {
    u32 t = game.state_t;
    if (t > DEMO_IDLE) {
        demo_start();
        return;
    }
    REG_BGHOFS(BG_SKY) = (u16)(t >> 2);
    REG_BGVOFS(BG_SKY) = 96;
    REG_BGHOFS(BG_MID) = (u16)(t >> 1);
    REG_BGVOFS(BG_MID) = 96;
    if (t == 1) {
        text_clear();
        u16* m = SCREENBLOCK(SBB_HUD);
        for (int y = 0; y < 6; y++)
            for (int x = 0; x < 30; x++) m[(y + 1) * 32 + x] = logo_map[y * 30 + x];
        text_put_center(8, "- ROBOT BLADE ACTION -", BGPAL_HUD_C);
        if (save_hiscore) {
            text_put(1, 15, "HI", BGPAL_HUD_C);
            text_num(4, 15, save_hiscore, 7, BGPAL_HUD);
            text_put(13, 15, "CHAIN", BGPAL_HUD_C);
            text_num(19, 15, save_best_chain, 3, BGPAL_HUD);
            if (save_cleared) text_put(23, 15, "* CLEAR", BGPAL_HUD_Y);
        }
        text_put_center(17, "B:SLASH  A:JUMP  R:DASH", BGPAL_HUD_C);
        text_put_center(19, "(C) 2026 STEEL CHAIN PROJECT", BGPAL_HUD);
    }
    /* 難易度選択 (左右で切り替え) */
    if (key_hit(KEY_LEFT | KEY_RIGHT | KEY_SELECT)) {
        /* 0=EASY 1=NORMAL 2=HARD: 左で易しく、右/SELECT で難しく (端で折り返し) */
        int d = game.easy ? 0 : game.hard ? 2 : 1;
        d = (d + (key_hit(KEY_LEFT) ? 2 : 1)) % 3;
        game.easy = (u8)(d == 0);
        game.hard = (u8)(d == 2);
        sfx_play(SFX_SELECT);
    }
    /* ステージセレクト (到達済みステージまで, 上下で選択) */
    if (save_reached > 0 && key_hit(KEY_UP | KEY_DOWN)) {
        int n = save_reached + 1;
        title_cursor = (title_cursor + (key_hit(KEY_UP) ? 1 : n - 1)) % n;
        sfx_play(SFX_SELECT);
    }
    if (t > 1) {
        text_put_center(10, game.easy ? "<  EASY   >" : game.hard ? "<  HARD   >" : "<  NORMAL >",
                        game.easy ? BGPAL_HUD_C : game.hard ? BGPAL_HUD_Y : BGPAL_HUD);
        if (save_reached > 0 || save_rank[0]) {
            /* 開始ステージと、そのステージの最高ランク */
            char buf[24] = "STAGE 1 ^v BEST -";
            buf[6] = (char)('1' + title_cursor);
            if (!save_reached) buf[8] = buf[9] = ' ';
            if (save_rank[title_cursor]) buf[16] = "CBAS"[save_rank[title_cursor] - 1];
            text_put_center(11, buf, title_cursor ? BGPAL_HUD_Y : BGPAL_HUD);
        }
        text_put_center(13, (t >> 5) & 1 ? "PRESS START" : "           ", BGPAL_HUD_Y);
    }
    oam_begin();
    oam_put(SPR_PLAYER_IDLE0 + ((t >> 5) & 1), 14, 74, 0, OBJPAL_NORMAL, 0);
    oam_put(SPR_PLAYER_IDLE0 + ((t >> 5) & 1), 194, 74, 1, OBJPAL_GHOST, 0);
    if (t > 20 && key_hit(KEY_START | KEY_A)) {
        sfx_play(SFX_SELECT);
        game.stage = (u8)title_cursor;
        game.lives = START_LIVES;
        game.score = 0;
        game.kills = 0;
        game.deaths = 0;
        game.chain_best = 0;
        game.total_time = 0;
        for (int i = 0; i < 5; i++) game.run_rank[i] = 0;
        game_set_state(GS_STAGE_INTRO);
    }
}
