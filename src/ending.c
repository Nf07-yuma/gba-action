/* エンディング: リザルト → スタッフロール → THE END */
#include "game.h"

/* ---- スタッフロール: BG0 を縦スクロールし、行を 32 行のリングに書き足していく ---- */
typedef struct { const char* s; u8 pal; s8 spr; } RollLine;
#define RL(s, p) {s, p, -1}
#define RS(spr) {0, 0, spr}
#define RB {"", 0, -1}
static const RollLine roll_lines[] = {
    RL("STEEL CHAIN", BGPAL_HUD_Y), RB, RB, RB, RB,
    RL("- STAFF -", BGPAL_HUD_C), RB, RB,
    RL("ORIGINAL CONCEPT", BGPAL_HUD_C), RL("THE PLAYER", BGPAL_HUD), RB, RB,
    RL("PROGRAM / GRAPHICS", BGPAL_HUD_C), RL("SOUND / LEVEL DESIGN", BGPAL_HUD_C), RL("CLAUDE CODE", BGPAL_HUD), RB, RB,
    RL("TESTING", BGPAL_HUD_C), RL("AUTO-PLAY BOT", BGPAL_HUD), RL("HEADLESS MGBA", BGPAL_HUD), RB, RB, RB, RB,
    RL("- CAST -", BGPAL_HUD_C), RB, RB,
    RS(SPR_PLAYER_RUN0), RB, RB, RB, RL("STEEL", BGPAL_HUD), RB, RB,
    RS(SPR_CRAWLER0), RB, RB, RL("CRAWLER", BGPAL_HUD), RB, RB,
    RS(SPR_DRONE0), RB, RB, RL("DRONE", BGPAL_HUD), RB, RB,
    RS(SPR_TURRET0), RB, RB, RL("TURRET", BGPAL_HUD), RB, RB,
    RS(SPR_JUMPER0), RB, RB, RL("JUMPER", BGPAL_HUD), RB, RB,
    RS(SPR_SHIELD0), RB, RB, RB, RL("SHIELD GUARD", BGPAL_HUD), RB, RB,
    RS(SPR_MINE0), RB, RB, RL("FLOAT MINE", BGPAL_HUD), RB, RB,
    RS(SPR_BOMBER0), RB, RB, RL("BOMBER DRONE", BGPAL_HUD), RB, RB,
    RS(SPR_LASER0), RB, RB, RL("LASER TURRET", BGPAL_HUD), RB, RB, RB,
    RS(SPR_BOSS_IDLE0), RB, RB, RB, RB, RB, RB, RB, RB, RL("GUARDIAN", BGPAL_HUD_Y), RB, RB, RB, RB, RB, RB, RB, RB, RB, RB, RB, RB, RB, RB,
    RS(SPR_CORE_OPEN), RB, RB, RB, RB, RB, RB, RB, RB, RL("OVERLORD CORE", BGPAL_HUD_Y), RB, RB, RB, RB, RB, RB, RB,
    RL("AND YOU", BGPAL_HUD_C), RB, RB, RB, RB, RB, RB, RB,
};
#define ROLL_COUNT ((int)(sizeof(roll_lines) / sizeof(roll_lines[0])))
#define ROLL_END   (ROLL_COUNT * 8)  /* 全行が画面上に抜けるスクロール量 (px) */
static u8 ending_phase;   /* 0=リザルト, 1=スタッフロール, 2=THE END */
static u16 roll_y;
static s16 roll_written;

static void roll_begin(void) {
    ending_phase = 1;
    roll_y = 0;
    roll_written = -1;
    text_clear();
    REG_BGVOFS(BG_HUD) = 0;
}

/* スクロール量に合わせて、これから画面下に入る行を書き込む */
static void roll_write_lines(void) {
    while (roll_written + 1 < ROLL_COUNT && (roll_written + 1) * 8 <= roll_y + 8) {
        int v = ++roll_written;
        int row = (20 + v) & 31;
        text_fill(0, row, 32, 1, 0, 0);
        if (roll_lines[v].s && roll_lines[v].s[0]) text_put_center(row, roll_lines[v].s, roll_lines[v].pal);
    }
}

static void roll_draw_sprites(u32 t) {
    for (int v = 0; v <= roll_written; v++) {
        int spr = roll_lines[v].spr;
        if (spr < 0) continue;
        int y = (20 + v) * 8 - roll_y;
        if (y <= -64 || y >= SCREEN_H) continue;
        /* 2 コマのアニメ (ボス/コアは 1 コマ) */
        if (spr == SPR_PLAYER_RUN0) spr += (int)((t >> 3) % 6);
        else if (spr == SPR_BOSS_IDLE0) spr += (int)((t >> 4) & 1);
        else if (spr != SPR_CORE_OPEN) spr += (int)((t >> 4) & 1);
        if (spr == SPR_BOSS_IDLE0 || spr == SPR_BOSS_IDLE1 || spr == SPR_CORE_OPEN) sprite_request_dynamic(spr);
        const SpriteFrame* f = &sprite_frames[spr];
        oam_put(spr, 120 - f->w / 2, y, 0, OBJPAL_NORMAL, 0);
    }
}

void ending_update(void) {
    u32 t = game.state_t;
    g_debug.ending_phase = ending_phase;
    if (t == 1) {
        ending_phase = 0;
        bg_dim = 0;
        REG_BGVOFS(BG_HUD) = 0;
        REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ;
        text_clear();
        music_play(SONG_ENDING);
        text_put_center(2, "MISSION COMPLETE", BGPAL_HUD_Y);
        if (save_record(game.score, game.chain_best, 1)) text_put_center(17, "NEW RECORD!", BGPAL_HUD_Y);
        int diff = game.easy ? 0 : game.hard ? 2 : 1;
        save_clear_difficulty(diff);
        text_put_center(4, "THE CORE IS SILENT.", BGPAL_HUD);
        text_put_center(5, "STEEL RETURNS HOME.", BGPAL_HUD);
        text_put(6, 7, "SCORE", BGPAL_HUD_C);
        text_num(17, 7, game.score, 7, BGPAL_HUD);
        text_put(6, 8, "BEST CHAIN", BGPAL_HUD_C);
        text_num(21, 8, game.chain_best, 3, BGPAL_HUD);
        text_put(6, 9, "DESTROYED", BGPAL_HUD_C);
        text_num(21, 9, game.kills, 3, BGPAL_HUD);
        text_put(6, 10, "MISSES", BGPAL_HUD_C);
        text_num(21, 10, game.deaths, 3, BGPAL_HUD);
        text_put(6, 11, "TOTAL TIME", BGPAL_HUD_C);
        text_time(19, 11, game.total_time, BGPAL_HUD);
        {
            /* 今回集めたデータチップ / 全ステージの総数 */
            int total = 0;
            for (int s = 0; s < LEVEL_COUNT; s++)
                for (int i = 0; i < levels[s].nobjs; i++) total += levels[s].objs[i].type == OBJ_CHIP;
            text_put(6, 12, "DATA CHIPS", BGPAL_HUD_C);
            text_num(18, 12, game.run_chips, 2, game.run_chips >= total ? BGPAL_HUD_Y : BGPAL_HUD);
            text_put(20, 12, "/", BGPAL_HUD);
            text_num(21, 12, (u32)total, 2, BGPAL_HUD);
            g_debug.run_chips = game.run_chips | ((u32)total << 8);
        }
        text_put(6, 13, "DIFFICULTY", BGPAL_HUD_C);
        text_put(18, 13, diff == 0 ? "  EASY" : diff == 2 ? "  HARD" : "NORMAL", diff == 2 ? BGPAL_HUD_Y : BGPAL_HUD);
        /* ステージ別ランク (今回のプレイ) */
        text_put(6, 15, "RANKS", BGPAL_HUD_C);
        for (int i = 0; i < 5; i++) {
            int r = game.run_rank[i];
            char c[2] = {r ? "CBAS"[r - 1] : '-', 0};
            text_put(14 + i * 2, 15, c, r >= 3 ? BGPAL_HUD_Y : BGPAL_HUD);
        }
        text_put_center(19, "THANK YOU FOR PLAYING", BGPAL_HUD_Y);
    }
    REG_BGHOFS(BG_SKY) = (u16)(t >> 2);
    REG_BGHOFS(BG_MID) = (u16)(t >> 1);
    oam_begin();
    if (ending_phase == 0) {
        oam_put(SPR_PLAYER_IDLE0 + ((t >> 5) & 1), 8, 76, 0, OBJPAL_NORMAL, 0);
        /* START か 10 秒経過でスタッフロールへ */
        if ((t > 120 && key_hit(KEY_START | KEY_A)) || t > 600) roll_begin();
        return;
    }
    if (ending_phase == 1) {
        /* 1 フレーム 0.5px。A を押している間は 4 倍速 */
        if (bg_dim < 8 && !(t & 3)) bg_dim++;  /* じわっと暗くする */
        int step = key_down(KEY_A) ? 2 : (t & 1);
        roll_y = (u16)(roll_y + step);
        roll_write_lines();
        REG_BGVOFS(BG_HUD) = (u16)(roll_y & 255);
        roll_draw_sprites(t);
        if (roll_y >= ROLL_END || key_hit(KEY_START)) {
            ending_phase = 2;
            game.state_t = 1;
            text_clear();
            REG_BGVOFS(BG_HUD) = 0;
            text_put_center(7, "THE END", BGPAL_HUD_Y);
            text_put_center(16, "PRESS START", BGPAL_HUD_C);
        }
        return;
    }
    /* THE END */
    oam_put(SPR_PLAYER_IDLE0 + ((t >> 5) & 1), 112, 80, 0, OBJPAL_NORMAL, 0);
    if (t > 60 && key_hit(KEY_START)) title_enter();
}
