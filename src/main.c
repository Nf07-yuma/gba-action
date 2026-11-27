/* STEEL CHAIN: メインループとゲーム進行 */
#include "game.h"

Game game;
static u8 loading_frame; /* マップ全再描画などの読み込みフレーム (計測対象外) */
__attribute__((section(".ewram_debug"))) volatile DebugInfo g_debug;

static u32 stage_start_score;

/* ---------------------------------------------------------------- HUD */
static u16 hud_last_hp = 0xFFFF, hud_last_lives = 0xFFFF, hud_last_chain = 0xFFFF;
static u32 hud_last_score = 0xFFFFFFFF;

/* CHAIN の節目 (10/25/50/100) で画面中央上に出るポップアップ */
#define POPUP_ROW 4
#define POPUP_LEN 100
static const u16 chain_marks[] = {10, 25, 50, 100};
static const char* const chain_words[] = {"NICE!", "GREAT!", "AMAZING!", "UNSTOPPABLE!"};
static u8 popup_t;
static char popup_text[24];

/* 画面上部に短いメッセージを出す (CHAIN の節目・中間地点など) */
void game_popup(const char* msg) {
    int i = 0;
    for (; msg[i] && i < 23; i++) popup_text[i] = msg[i];
    popup_text[i] = 0;
    popup_t = POPUP_LEN;
}

static void popup_draw(void) {
    if (!popup_t) return;
    popup_t--;
    text_put(0, POPUP_ROW, "                              ", BGPAL_HUD);
    /* 最初は点滅、最後の 20 フレームで消える */
    int elapsed = POPUP_LEN - popup_t;
    if (popup_t > 0 && (popup_t > 20 || (popup_t & 2)) && (elapsed >= 24 || !(elapsed & 4)))
        text_put_center(POPUP_ROW, popup_text, BGPAL_HUD_Y);
}

void hud_draw(int force) {
    if (force) {
        text_clear();
        hud_last_hp = hud_last_lives = hud_last_chain = 0xFFFF;
        hud_last_score = 0xFFFFFFFF;
        popup_t = 0;
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
        int lx = player.hp_max + 2;
        m[lx] = HUD_TILE_LIFE_ICON | (BGPAL_HUD << 12);
        text_put(lx + 1, 0, "x", BGPAL_HUD);
        text_num(lx + 2, 0, (u32)MAX(0, game.lives), 1, BGPAL_HUD);
    }
    if (hud_last_score != game.score) {
        hud_last_score = game.score;
        text_num(23, 0, game.score, 7, BGPAL_HUD);
    }
    u16 ch = game.chain >= 2 ? game.chain : 0;
    if (hud_last_chain != ch) {
        hud_last_chain = ch;
        if (ch) {
            text_num(21, 1, ch, 3, BGPAL_HUD_Y);
            text_put(24, 1, " CHAIN", BGPAL_HUD_Y);
        } else {
            text_put(21, 1, "         ", BGPAL_HUD);
        }
    }
    popup_draw();
}

/* ---------------------------------------------------------- ゲーム API */
void game_add_score(u32 pts) {
    game.score += pts;
    if (game.score > 9999999) game.score = 9999999;
}

void game_add_chain(int hits) {
    u16 before = game.chain;
    game.chain += (u16)hits;
    for (unsigned i = 0; i < sizeof(chain_marks) / sizeof(chain_marks[0]); i++) {
        u16 mk = chain_marks[i];
        if (before < mk && game.chain >= mk) {
            /* "25 CHAIN GREAT!" とボーナス (節目 x 50 点) */
            char* p = popup_text;
            if (mk >= 100) *p++ = (char)('0' + mk / 100);
            *p++ = (char)('0' + (mk / 10) % 10);
            *p++ = (char)('0' + mk % 10);
            const char* w = " CHAIN ";
            while (*w) *p++ = *w++;
            w = chain_words[i];
            while (*w) *p++ = *w++;
            *p = 0;
            popup_t = POPUP_LEN;
            game_add_score(mk * 50u);
            sfx_play(SFX_CHARGE);
            g_debug.popups++;
        }
    }
    game.chain_t = 120;
    if (game.chain > game.chain_best) game.chain_best = game.chain;
    if (game.chain > game.stage_chain_best) game.stage_chain_best = game.chain;
    game_add_score(10u * MIN(game.chain, 50));
}

void game_set_checkpoint(s32 x, s32 y) {
    game.checkpoint_x = x;
    game.checkpoint_y = y;
}

void game_set_state(int s) {
    game.state = (u8)s;
    game.state_t = 0;
}

void game_stage_clear(void) {
    if (game.state != GS_PLAY) return;
    player.state = PS_CLEAR;
    player.state_t = 0;
    game_set_state(GS_CLEAR);
    sfx_play(SFX_CHECK);
    music_play(SONG_CLEAR);
}

void game_boss_defeated(int x, int y) {
    (void)x;
    (void)y;
    game_stage_clear();
}

void game_on_player_dead(void) {
    game.deaths++;
    game.stage_deaths++;
    game_set_state(GS_DEAD);
}

/* --------------------------------------------------------- ステージ開始 */
static void stage_begin(int respawn) {
    if (!respawn) {
        level_load(game.stage);
        enemies_reset(1);
        game.checkpoint_x = level.def->spawn_x * CELL + CELL / 2;
        game.checkpoint_y = (level.def->spawn_y + 1) * CELL;
        stage_start_score = game.score;
        bg_dim = 0;
        game.stage_time = 0;
        game.stage_chain_best = 0;
        game.stage_deaths = 0;
        game.stage_hurt = 0;
    } else {
        enemies_reset(0);
    }
    bullets_clear();
    fx_clear();
    boss_reset();
    player_waves_clear();
    player_init(game.checkpoint_x - 5, game.checkpoint_y - 24);
    game.chain = 0;
    game.chain_t = 0;
    game.hitstop = 0;
    game.slowmo = 0;
    camera_snap(game.checkpoint_x, game.checkpoint_y - 12);
    level_force_redraw();
    loading_frame = 2;
    hud_draw(1);
}

static void draw_world(void) {
    oam_begin();
    player_draw();
    fx_draw();
    bullets_draw();
    enemies_draw();
    boss_draw();
}

/* ステージ開始時の司令部からの通信 (各 2 行, 28 文字以内) */
static const char* const stage_msg[5][2] = {
    {"HQ: FACTORY GATE IS OVERRUN.", "HOLD B TO CHARGE A SLASH."},
    {"HQ: TAKE THE SKYWAY ROUTE.", "WATCH YOUR STEP UP THERE."},
    {"HQ: A HEAVY GUARD UNIT IS", "WAITING IN THE FOUNDRY."},
    {"HQ: DATA HIGHWAY AHEAD.", "RIDE THE LIFTS ACROSS."},
    {"HQ: THIS IS IT, STEEL.", "SILENCE THE OVERLORD CORE."},
};
#define INTRO_LEN 190

/* 1 文字ずつ表示 (タイプライター) */
static void intro_message(u32 t) {
    if (t < 24) return;
    u32 n = (t - 24) / 2;
    const char* const* m = stage_msg[game.stage < 5 ? game.stage : 0];
    for (int line = 0; line < 2; line++) {
        const char* s = m[line];
        int len = 0;
        while (s[len]) len++;
        int show = (int)n - (line ? 30 : 0);
        if (show < 0) break;
        if (show > len) show = len;
        if (show == len && (int)n - (line ? 30 : 0) > len) continue;
        char buf[32];
        for (int i = 0; i < show; i++) buf[i] = s[i];
        buf[show] = 0;
        text_put(1, 15 + line * 2, buf, line ? BGPAL_HUD : BGPAL_HUD_C);
        if (show < len && (t & 3) == 0 && s[show - 1 >= 0 ? show - 1 : 0] != ' ') sfx_play(SFX_SELECT);
    }
}

static void intro_update(void) {
    if (game.state_t == 1) {
        REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ;
        stage_begin(0);
        if (!game.demo) save_reach_stage(game.stage);
        {
            static const u8 stage_song[] = {SONG_STAGE, SONG_STAGE2, SONG_FOUNDRY, SONG_HIGHWAY, SONG_FINAL};
            music_play(stage_song[game.stage < 5 ? game.stage : 0]);
        }
        char buf[16] = "STAGE 1";
        buf[6] = (char)('1' + game.stage);
        text_put_center(7, buf, BGPAL_HUD_Y);
        text_put_center(9, level.def->name, BGPAL_HUD);
        text_put_center(12, "READY", BGPAL_HUD_C);
        text_fill(0, 14, 30, 5, HUD_TILE_SOLID_DARK, BGPAL_HUD);
    }
    intro_message(game.state_t);
    level_scroll_update();
    draw_world();
    if (game.state_t > INTRO_LEN || (game.state_t > 20 && key_hit(KEY_START))) {
        for (int y = 7; y < 19; y++) text_put(0, y, "                              ", BGPAL_HUD);
        game_set_state(GS_PLAY);
    }
}

static void world_step(void) {
    game.stage_time++;
    game.total_time++;
    int slow_skip = 0;
    if (game.slowmo) {
        game.slowmo--;
        slow_skip = game.slowmo & 1;
    }
    if (game.hitstop) {
        game.hitstop--;
    } else if (!slow_skip) {
        player_update();
        enemies_spawn_check();
        enemies_update();
        boss_update();
        bullets_update();
    }
    if (!slow_skip) {
        fx_ambient();
        fx_update();
    }
    if (game.chain_t && --game.chain_t == 0) game.chain = 0;
    camera_update(FROM_FP(player.b.x) + player.b.w / 2, FROM_FP(player.b.y) + player.b.h / 2, player.dir);
    level_scroll_update();
}

static int pause_cursor;
static void pause_menu_draw(void) {
    static const char* items[3] = {"RESUME", "RETRY STAGE", "QUIT TO TITLE"};
    text_fill(8, 6, 14, 9, HUD_TILE_SOLID_DARK, BGPAL_HUD);
    text_put_center(7, "- PAUSE -", BGPAL_HUD_Y);
    for (int i = 0; i < 3; i++) {
        if (i == pause_cursor) text_put(9, 9 + i * 2, ">", BGPAL_HUD_Y);
        else text_fill(9, 9 + i * 2, 1, 1, HUD_TILE_SOLID_DARK, BGPAL_HUD);
        text_put(11, 9 + i * 2, items[i], i == pause_cursor ? BGPAL_HUD_C : BGPAL_HUD);
    }
    bg_dim = 10; /* 背景を暗くして文字を読みやすく */
    /* 操作のおさらい */
    text_put_center(16, "B SLASH  A JUMP  R DASH", BGPAL_HUD_C);
    text_put_center(17, "HOLD B: CHARGE SLASH", BGPAL_HUD);
    text_put_center(18, "DASH + B: DASH SLASH", BGPAL_HUD);
}

static void pause_menu_clear(void) {
    for (int y = 6; y < 19; y++) text_put(0, y, "                              ", BGPAL_HUD);
    bg_dim = 0;
}

static void play_update(void) {
    if (key_hit(KEY_START)) {
        game_set_state(GS_PAUSE);
        pause_cursor = 0;
        pause_menu_draw();
        sfx_play(SFX_SELECT);
        music_pause(1);
        return;
    }
    world_step();
    /* HP が残りわずか: 一定間隔で控えめな警告音 (HUD のゲージも赤くなる) */
    if (player.hp > 0 && player.hp <= 2 && player.state != PS_DEAD && !game.demo && game.stage_time % 80 == 0)
        sfx_play(SFX_LOWHP);
    draw_world();
}

static void pause_update(void) {
    draw_world();
    if (key_hit(KEY_UP | KEY_DOWN)) {
        pause_cursor = (pause_cursor + (key_hit(KEY_DOWN) ? 1 : 2)) % 3;
        sfx_play(SFX_SELECT);
        pause_menu_draw();
    }
    if (key_hit(KEY_START) && pause_cursor != 0) pause_cursor = 0; /* START は常に再開 */
    if (key_hit(KEY_START | KEY_A)) {
        pause_menu_clear();
        sfx_play(SFX_SELECT);
        if (pause_cursor == 0) {
            music_pause(0);
            game_set_state(GS_PLAY);
        } else if (pause_cursor == 1) {
            /* ステージ最初からやり直し (スコアはステージ開始時に戻す) */
            game.score = stage_start_score;
            game_set_state(GS_STAGE_INTRO);
        } else {
            save_record(game.score, game.chain_best, 0);
            title_enter();
        }
    }
}

static void dead_update(void) {
    world_step();
    draw_world();
    if (game.state_t > 100) {
        if (game.lives > 0) {
            game.lives--;
            stage_begin(1);
            game_set_state(GS_PLAY);
        } else {
            game_set_state(GS_GAMEOVER);
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
        /* タイムボーナス: 3 分以内なら残り秒数 x 20 */
        u32 sec = game.stage_time / 60;
        u32 tbonus = sec < 180 ? (180 - sec) * 20 : 0;
        text_put_center(5, "STAGE CLEAR!", BGPAL_HUD_Y);
        text_put(7, 8, "TIME", BGPAL_HUD_C);
        text_time(20, 8, game.stage_time, BGPAL_HUD);
        /* ベストタイム (デモでは記録しない) */
        g_debug.best_time = 0;
        u16 prev_best = save_best_time[game.stage < 5 ? game.stage : 0];
        if (!game.demo && save_time_stage(game.stage, game.stage_time)) {
            text_put(7, 9, prev_best ? "NEW BEST TIME!" : "FIRST CLEAR!", BGPAL_HUD_Y);
            g_debug.best_time = 1;
        } else if (prev_best) {
            text_put(7, 9, "BEST", BGPAL_HUD);
            text_time(20, 9, prev_best, BGPAL_HUD);
        }
        text_put(7, 10, "TIME BONUS", BGPAL_HUD);
        text_num(20, 10, tbonus, 5, BGPAL_HUD);
        text_put(7, 12, "CHAIN BONUS", BGPAL_HUD);
        text_num(20, 12, game.chain_best * 100u, 5, BGPAL_HUD);
        text_put(7, 14, "LIFE BONUS", BGPAL_HUD);
        text_num(20, 14, (u32)MAX(0, player.hp) * 200u, 5, BGPAL_HUD);
        game_add_score(tbonus + game.chain_best * 100u + (u32)MAX(0, player.hp) * 200u);
        /* ノーダメージ (ミスも被弾もなし) ならボーナス */
        g_debug.nodamage = 0;
        if (game.stage_hurt == 0 && game.stage_deaths == 0) {
            text_put(7, 15, "NO DAMAGE!", BGPAL_HUD_Y);
            text_num(20, 15, 5000, 5, BGPAL_HUD_Y);
            game_add_score(5000);
            g_debug.nodamage = 1;
        }
        g_debug.clears++;
    }
    if (t == 110) {
        /* ランク評価: タイム・ステージ内最大 CHAIN・ミス回数・残り HP の合計点 (0-7) */
        u32 sec = game.stage_time / 60;
        int pts = (sec < 100 ? 2 : sec < 160 ? 1 : 0) + (game.stage_chain_best >= 25 ? 2 : game.stage_chain_best >= 10 ? 1 : 0) +
                  (game.stage_deaths == 0 ? 2 : game.stage_deaths == 1 ? 1 : 0) + (player.hp >= player.hp_max / 2 ? 1 : 0);
        static const char ranks[] = "CBAS";
        int r = pts >= 6 ? 3 : pts >= 4 ? 2 : pts >= 2 ? 1 : 0;
        char buf[12] = "RANK  ?";
        buf[6] = ranks[r];
        text_put_center(16, buf, r >= 2 ? BGPAL_HUD_Y : BGPAL_HUD);
        if (!game.demo && save_rank_stage(game.stage, r)) text_put(20, 16, "BEST!", BGPAL_HUD_C);
        sfx_play(r == 3 ? SFX_CHARGE : SFX_CHECK);
        g_debug.rank = (u32)r;
        if (game.stage < 5) game.run_rank[game.stage] = (u8)(r + 1);
        g_debug.rank_hist |= (u32)(r + 1) << (game.stage * 4);
    }
    if (bg_dim < 12 && (t & 1)) bg_dim++;
    level_scroll_update();
    oam_begin(); /* 集計中はスプライトを消して文字を読みやすく */
    if (t > 240 || (t > 120 && key_hit(KEY_START | KEY_A))) {
        if (game.stage + 1 < LEVEL_COUNT) {
            game.stage++;
            game_set_state(GS_STAGE_INTRO);
        } else {
            game_set_state(GS_ENDING);
        }
    }
}

static void gameover_update(void) {
    if (game.state_t == 1) {
        music_play(SONG_GAMEOVER);
        bg_dim = 10;
        text_clear();
        text_put_center(7, "GAME OVER", BGPAL_HUD);
        if (save_record(game.score, game.chain_best, 0)) text_put_center(9, "NEW RECORD!", BGPAL_HUD_Y);
        text_put_center(11, "START: CONTINUE", BGPAL_HUD_C);
        text_put_center(13, "SELECT: TITLE", BGPAL_HUD_C);
    }
    oam_begin();
    /* コンティニューのカウントダウン (10 秒で自動的にタイトルへ) */
    if (game.state_t >= 30) {
        u32 left = (game.state_t - 30) / 60;
        if (left >= 10) {
            title_enter();
            return;
        }
        char buf[16] = "CONTINUE? 9";
        buf[10] = (char)('9' - left);
        text_put_center(15, buf, left >= 7 ? BGPAL_HUD_Y : BGPAL_HUD);
        if ((game.state_t - 30) % 60 == 0) sfx_play(SFX_SELECT);
    }
    if (game.state_t > 30 && key_hit(KEY_START | KEY_A)) {
        game.lives = START_LIVES;
        game.score = stage_start_score / 2;
        game_set_state(GS_STAGE_INTRO);
    } else if (game.state_t > 30 && key_hit(KEY_SELECT)) {
        title_enter();
    }
}

/* --------------------------------------------------------------- main */
/* テスト用デバッグ情報 (EWRAM 先頭の DebugInfo) を更新 */
static void debug_publish(void) {
    g_debug.frame = game.frame;
    g_debug.song = (u32)(music_current() & 0xFF);
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
    g_debug.level_cells = level.def ? (u32)level.def->cells : 0;
    g_debug.level_w = (u32)level.w;
    g_debug.level_h = (u32)level.h;
    g_debug.vy = player.b.vy;
    g_debug.on_ground = player.b.on_ground;
    g_debug.boss_hp = boss_hp();
    {
        int n = 0, a, b, c, d2;
        u8 id;
        for (int k = 0; k < MAX_PWAVE; k++) n += player_wave_box(k, &a, &b, &c, &d2, &id);
        g_debug.pwaves = (u32)n;
        g_debug.charge = player.charge;
    g_debug.combo = player.combo | (player.state == PS_ATTACK ? 0x100 : 0);
    }
    g_debug.boss_state = (u32)boss_state_id();
    g_debug.boss_x = boss_x();
    {
        s32 xy[8];
        int n = lifts_debug(xy, 4);
        g_debug.lift_count = (u32)n;
        for (int i = 0; i < 8; i++) g_debug.lifts[i] = i < n * 2 ? xy[i] : 0;
    }
}

static volatile u32 vblank_count;
static void on_irq(u32 flags) {
    if (flags & IRQ_VBLANK) vblank_count++;
}

int main(void) {
    REG_WAITCNT = 0x4317;
    video_init();
    save_load();
    sound_init();
    irq_user_hook = on_irq;
    REG_DISPSTAT |= 0x0008;
    REG_IE = IRQ_VBLANK;
    REG_IME = 1;

    g_debug.magic = DEBUG_MAGIC;
    title_enter();

    u32 last_vb = 0;
    for (;;) {
        /* 処理時間の計測: VBlank 開始(160) からの経過走査線 */
        u32 v = REG_VCOUNT;
        u32 lines = (v + 228 - 160) % 228;
        int measure = game.frame > 2 && !loading_frame && game.state != GS_TITLE && game.state != GS_ENDING;
        if (loading_frame) loading_frame--;
        if (measure && lines > g_debug.max_lines) {
            g_debug.max_lines = lines;
            g_debug.max_state = game.state | (game.state_t << 8);
        }
        vsync();
        if (measure && vblank_count - last_vb > 1) {
            g_debug.lag_frames++;
            g_debug.lag_state = game.state | (game.state_t << 8);
        }
        last_vb = vblank_count;
        oam_end();
        sprite_flush_dynamic();
        fx_flash_apply();
        hud_draw(0);
        if (game.state == GS_PLAY || game.state == GS_DEAD || game.state == GS_PAUSE) boss_hud();
        sound_update();
        input_update();
        if (game.demo) demo_input();
        game.frame++;
        /* テスト用命令 */
        if (g_debug.cmd == 1 && game.state != GS_STAGE_INTRO) {
            game.stage = (u8)g_debug.cmd_arg;
            if (game.lives <= 0) game.lives = START_LIVES;
            game_set_state(GS_STAGE_INTRO);
            g_debug.cmd = 0;
        } else if (g_debug.cmd == 5 && game.state == GS_PLAY) {
            /* テスト用: 残機 0 にしてミス (ゲームオーバー画面へ) */
            game.lives = 0;
            player_kill();
            g_debug.cmd = 0;
        } else if (g_debug.cmd == 6) {
            /* テスト用: クリア済みにする (セーブはしない)。タイトルを描き直す */
            save_cleared = 1;
            if (game.state == GS_TITLE) game.state_t = 0;
            g_debug.cmd = 0;
        } else if (g_debug.cmd == 4) {
            music_stop();
            sfx_play((int)g_debug.cmd_arg);
            g_debug.cmd = 0;
        } else if (g_debug.cmd == 3) {
            music_play((int)g_debug.cmd_arg);
            g_debug.cmd = 0;
        } else if (g_debug.cmd == 2 && game.state == GS_PLAY) {
            player.b.x = TO_FP(g_debug.cmd_arg);
            player.b.y = TO_FP((level.h - 5) * CELL);
            player.b.vy = 0;
            camera_snap(g_debug.cmd_arg, 80);
            level_force_redraw();
            loading_frame = 2;
            g_debug.cmd = 0;
        }
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
        debug_publish();
    }
}
