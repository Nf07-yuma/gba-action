/* STEEL CHAIN: メインループとゲーム進行 */
#include "game.h"

Game game;
__attribute__((section(".ewram_debug"))) volatile DebugInfo g_debug;

static u32 stage_start_score;
static int title_cursor;

/* ---------------------------------------------------------------- HUD */
static u16 hud_last_hp = 0xFFFF, hud_last_lives = 0xFFFF, hud_last_chain = 0xFFFF;
static u32 hud_last_score = 0xFFFFFFFF;

void hud_draw(int force) {
    if (force) {
        text_clear();
        hud_last_hp = hud_last_lives = hud_last_chain = 0xFFFF;
        hud_last_score = 0xFFFFFFFF;
    }
    if (game.state != GS_PLAY && game.state != GS_DEAD && game.state != GS_CLEAR && game.state != GS_STAGE_INTRO &&
        game.state != GS_PAUSE)
        return;
    u16* m = SCREENBLOCK(SBB_HUD);
    if (hud_last_hp != (u16)player.hp) {
        hud_last_hp = (u16)player.hp;
        m[0] = HUD_TILE_HP_LABEL | (BGPAL_HUD << 12);
        for (int i = 0; i < player.hp_max; i++) {
            int t = i < player.hp ? (player.hp <= 2 ? HUD_TILE_HP_LOW : HUD_TILE_HP_FULL) : HUD_TILE_HP_EMPTY;
            m[1 + i] = (u16)(t | (BGPAL_HUD << 12));
        }
    }
    if (hud_last_lives != (u16)game.lives) {
        hud_last_lives = (u16)game.lives;
        m[10] = HUD_TILE_LIFE_ICON | (BGPAL_HUD << 12);
        text_put(11, 0, "x", BGPAL_HUD);
        text_num(12, 0, (u32)MAX(0, game.lives), 1, BGPAL_HUD);
    }
    if (hud_last_score != game.score) {
        hud_last_score = game.score;
        text_num(23, 0, game.score, 7, BGPAL_HUD);
    }
    u16 ch = game.chain >= 2 ? game.chain : 0;
    if (hud_last_chain != ch) {
        hud_last_chain = ch;
        if (ch) {
            text_num(14, 0, ch, 3, BGPAL_HUD_Y);
            text_put(17, 0, " CHAIN", BGPAL_HUD_Y);
        } else {
            text_put(14, 0, "         ", BGPAL_HUD);
        }
    }
}

/* ---------------------------------------------------------- ゲーム API */
void game_add_score(u32 pts) {
    game.score += pts;
    if (game.score > 9999999) game.score = 9999999;
}

void game_add_chain(int hits) {
    game.chain += (u16)hits;
    game.chain_t = 120;
    if (game.chain > game.chain_best) game.chain_best = game.chain;
    game_add_score(10u * MIN(game.chain, 50));
}

void game_set_checkpoint(s32 x, s32 y) {
    game.checkpoint_x = x;
    game.checkpoint_y = y;
}

static void set_state(int s) {
    game.state = (u8)s;
    game.state_t = 0;
}

void game_stage_clear(void) {
    if (game.state != GS_PLAY) return;
    player.state = PS_CLEAR;
    player.state_t = 0;
    set_state(GS_CLEAR);
    sfx_play(SFX_CHECK);
    music_stop();
}

void game_on_player_dead(void) {
    game.deaths++;
    set_state(GS_DEAD);
}

/* --------------------------------------------------------- ステージ開始 */
static void stage_begin(int respawn) {
    if (!respawn) {
        level_load(game.stage);
        enemies_reset(1);
        game.checkpoint_x = level.def->spawn_x * CELL + CELL / 2;
        game.checkpoint_y = (level.def->spawn_y + 1) * CELL;
        stage_start_score = game.score;
    } else {
        enemies_reset(0);
    }
    bullets_clear();
    fx_clear();
    player_init(game.checkpoint_x - 5, game.checkpoint_y - 24);
    game.chain = 0;
    game.chain_t = 0;
    game.hitstop = 0;
    camera_snap(game.checkpoint_x, game.checkpoint_y - 12);
    level_force_redraw();
    hud_draw(1);
}

static void draw_world(void) {
    oam_begin();
    player_draw();
    fx_draw();
    bullets_draw();
    enemies_draw();
}

/* --------------------------------------------------------------- 各画面 */
static void title_enter(void) {
    set_state(GS_TITLE);
    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG2 | DCNT_OBJ;
    text_clear();
    cam_x = 0;
    cam_y = 0;
    title_cursor = 0;
    music_play(SONG_TITLE);
}

static void title_update(void) {
    u32 t = game.state_t;
    REG_BGHOFS(BG_SKY) = (u16)(t >> 1);
    REG_BGVOFS(BG_SKY) = 96;
    if (t == 1) {
        text_clear();
        text_put_center(3, "=====================", BGPAL_HUD_C);
        text_put_center(5, "S T E E L   C H A I N", BGPAL_HUD);
        text_put_center(7, "=====================", BGPAL_HUD_C);
        text_put_center(17, "B:SLASH  A:JUMP  R:DASH", BGPAL_HUD_C);
        text_put_center(19, "(C) 2026 STEEL CHAIN PROJECT", BGPAL_HUD);
    }
    if (t > 1) text_put_center(14, (t >> 5) & 1 ? "PRESS START" : "           ", BGPAL_HUD_Y);
    oam_begin();
    oam_put(SPR_PLAYER_IDLE0 + ((t >> 5) & 1), 104, 72, 0, OBJPAL_NORMAL, 0);
    if (key_hit(KEY_START | KEY_A)) {
        sfx_play(SFX_SELECT);
        game.stage = 0;
        game.lives = 3;
        game.score = 0;
        game.kills = 0;
        game.deaths = 0;
        game.chain_best = 0;
        set_state(GS_STAGE_INTRO);
    }
}

static void intro_update(void) {
    if (game.state_t == 1) {
        REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_OBJ;
        stage_begin(0);
        music_play(SONG_STAGE);
        char buf[16] = "STAGE 1";
        buf[6] = (char)('1' + game.stage);
        text_put_center(7, buf, BGPAL_HUD_Y);
        text_put_center(9, level.def->name, BGPAL_HUD);
        text_put_center(12, "READY", BGPAL_HUD_C);
    }
    level_scroll_update();
    draw_world();
    if (game.state_t > 100 || (game.state_t > 20 && key_hit(KEY_START))) {
        text_put(0, 7, "                              ", BGPAL_HUD);
        text_put(0, 9, "                              ", BGPAL_HUD);
        text_put(0, 12, "                              ", BGPAL_HUD);
        set_state(GS_PLAY);
    }
}

static void world_step(void) {
    if (game.hitstop) {
        game.hitstop--;
    } else {
        player_update();
        enemies_spawn_check();
        enemies_update();
        bullets_update();
    }
    fx_update();
    if (game.chain_t && --game.chain_t == 0) game.chain = 0;
    camera_update(FROM_FP(player.b.x) + player.b.w / 2, FROM_FP(player.b.y) + player.b.h / 2, player.dir);
    level_scroll_update();
}

static void play_update(void) {
    if (key_hit(KEY_START)) {
        set_state(GS_PAUSE);
        text_put_center(8, "- PAUSE -", BGPAL_HUD_Y);
        sfx_play(SFX_SELECT);
        music_pause(1);
        return;
    }
    world_step();
    draw_world();
}

static void pause_update(void) {
    draw_world();
    if (key_hit(KEY_START)) {
        text_put(0, 8, "                              ", BGPAL_HUD);
        music_pause(0);
        set_state(GS_PLAY);
    }
}

static void dead_update(void) {
    world_step();
    draw_world();
    if (game.state_t > 100) {
        if (game.lives > 0) {
            game.lives--;
            stage_begin(1);
            set_state(GS_PLAY);
        } else {
            set_state(GS_GAMEOVER);
        }
    }
}

static void clear_update(void) {
    u32 t = game.state_t;
    if (t < 70) {
        player_update();
        fx_update();
        level_scroll_update();
        if ((t & 7) == 0) fx_spawn(FX_SPARK, FROM_FP(player.b.x) - 4 + rng_range(16), FROM_FP(player.b.y) + rng_range(24), 0);
        draw_world();
        return;
    }
    if (t == 70) {
        player.state = PS_DEAD; /* 非表示 */
        text_put_center(6, "STAGE CLEAR!", BGPAL_HUD_Y);
        text_put(8, 9, "CHAIN BONUS", BGPAL_HUD);
        text_num(20, 9, game.chain_best * 100u, 5, BGPAL_HUD);
        text_put(8, 11, "LIFE BONUS", BGPAL_HUD);
        text_num(20, 11, (u32)MAX(0, player.hp) * 200u, 5, BGPAL_HUD);
        game_add_score(game.chain_best * 100u + (u32)MAX(0, player.hp) * 200u);
        g_debug.clears++;
    }
    level_scroll_update();
    draw_world();
    if (t > 220 || (t > 100 && key_hit(KEY_START | KEY_A))) {
        if (game.stage + 1 < LEVEL_COUNT) {
            game.stage++;
            set_state(GS_STAGE_INTRO);
        } else {
            set_state(GS_ENDING);
        }
    }
}

static void gameover_update(void) {
    if (game.state_t == 1) {
        music_stop();
        text_clear();
        text_put_center(7, "GAME OVER", BGPAL_HUD);
        text_put_center(11, "START: CONTINUE", BGPAL_HUD_C);
        text_put_center(13, "SELECT: TITLE", BGPAL_HUD_C);
    }
    oam_begin();
    if (game.state_t > 30 && key_hit(KEY_START | KEY_A)) {
        game.lives = 3;
        game.score = stage_start_score / 2;
        set_state(GS_STAGE_INTRO);
    } else if (game.state_t > 30 && key_hit(KEY_SELECT)) {
        title_enter();
    }
}

static void ending_update(void) {
    u32 t = game.state_t;
    if (t == 1) {
        REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG2 | DCNT_OBJ;
        text_clear();
        music_play(SONG_TITLE);
        text_put_center(3, "MISSION COMPLETE", BGPAL_HUD_Y);
        text_put_center(6, "THE CORE IS SILENT.", BGPAL_HUD);
        text_put_center(7, "STEEL RETURNS HOME.", BGPAL_HUD);
        text_put(6, 10, "SCORE", BGPAL_HUD_C);
        text_num(17, 10, game.score, 7, BGPAL_HUD);
        text_put(6, 12, "BEST CHAIN", BGPAL_HUD_C);
        text_num(21, 12, game.chain_best, 3, BGPAL_HUD);
        text_put(6, 14, "DESTROYED", BGPAL_HUD_C);
        text_num(21, 14, game.kills, 3, BGPAL_HUD);
        text_put_center(18, "THANK YOU FOR PLAYING", BGPAL_HUD_Y);
    }
    REG_BGHOFS(BG_SKY) = (u16)(t >> 2);
    oam_begin();
    oam_put(SPR_PLAYER_IDLE0 + ((t >> 5) & 1), 104, 112 - 72, 0, OBJPAL_NORMAL, 0);
    if (t > 120 && key_hit(KEY_START)) title_enter();
}

/* --------------------------------------------------------------- main */
int main(void) {
    REG_WAITCNT = 0x4317;
    video_init();
    sound_init();
    REG_DISPSTAT |= 0x0008;
    REG_IE = IRQ_VBLANK;
    REG_IME = 1;

    g_debug.magic = DEBUG_MAGIC;
    title_enter();

    for (;;) {
        vsync();
        oam_end();
        hud_draw(0);
        sound_update();
        input_update();
        game.frame++;
        game.state_t++;
        switch (game.state) {
        case GS_TITLE: title_update(); break;
        case GS_STAGE_INTRO: intro_update(); break;
        case GS_PLAY: play_update(); break;
        case GS_PAUSE: pause_update(); break;
        case GS_DEAD: dead_update(); break;
        case GS_CLEAR: clear_update(); break;
        case GS_GAMEOVER: gameover_update(); break;
        case GS_ENDING: ending_update(); break;
        }
        /* テスト用デバッグ情報 */
        g_debug.frame = game.frame;
        g_debug.state = game.state;
        g_debug.stage = game.stage;
        g_debug.px = FROM_FP(player.b.x);
        g_debug.py = FROM_FP(player.b.y);
        g_debug.hp = player.hp;
        g_debug.lives = game.lives;
        g_debug.score = game.score;
        g_debug.kills = game.kills;
        g_debug.deaths = game.deaths;
        g_debug.chain_best = game.chain_best;
        g_debug.enemies = (u32)enemies_alive();
        g_debug.cam_x = (u32)cam_x;
    }
}
