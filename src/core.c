/* 最終ボス「OVERLORD CORE」
 *  閉じている間は無敵で全方位弾を撃つ → 装甲が開くと弱点の目が露出して攻撃が通る。
 *  HP 半分で第 2 形態: 横一列のレーザー(予告付きの高速弾列)とドローン召喚が加わる。
 */
#include "game.h"

enum { CS_NONE, CS_INTRO, CS_CLOSED, CS_OPENING, CS_OPEN, CS_CLOSING, CS_DEAD };

#define CW 36
#define CH 36
#define CORE_HP 44

typedef struct {
    u8 state, phase, flash, last_hit;
    u16 t, cycle;
    s16 hp, hp_max;
    s32 x, y;        /* 中心 (ピクセル) */
    s32 home_x, top_y, low_y;
    s16 laser_y;     /* レーザー予告中の高さ (-1 = なし) */
    u8 laser_t;
} Core;

static Core core;
extern s32 boss_lock_x;

int core_active(void) { return core.state != CS_NONE; }
int core_hp(void) { return core.state == CS_NONE ? 0 : core.hp; }
int core_hp_max(void) { return core.hp_max; }
int core_state_id(void) { return core.state; }
int core_x(void) { return core.x; }
int core_dead(void) { return core.state == CS_DEAD; }

void core_reset(void) {
    memset(&core, 0, sizeof(core));
    core.laser_y = -1;
}

static int pcx(void) { return FROM_FP(player.b.x) + player.b.w / 2; }
static int pcy(void) { return FROM_FP(player.b.y) + player.b.h / 2; }

void core_start(int spawn_cx, int spawn_cy) {
    core_reset();
    core.state = CS_INTRO;
    core.hp = core.hp_max = CORE_HP;
    core.home_x = spawn_cx * CELL + CELL / 2;
    int floor = (spawn_cy + 1) * CELL;
    core.low_y = floor - 40;   /* 開いたとき: 地上の斬撃が届く高さ */
    core.top_y = floor - 92;   /* 閉じているとき: 高めに浮く */
    core.x = core.home_x;
    core.y = -40;
}

/* 8 方向 (第2形態は 12 方向) のリング弾 */
static void ring_shot(int n, int offset) {
    static const s16 dir12[12][2] = {
        {0x200, 0}, {0x1BB, 0x100}, {0x100, 0x1BB}, {0, 0x200}, {-0x100, 0x1BB}, {-0x1BB, 0x100},
        {-0x200, 0}, {-0x1BB, -0x100}, {-0x100, -0x1BB}, {0, -0x200}, {0x100, -0x1BB}, {0x1BB, -0x100},
    };
    static const s16 dir8[8][2] = {
        {0x200, 0}, {0x16A, 0x16A}, {0, 0x200}, {-0x16A, 0x16A}, {-0x200, 0}, {-0x16A, -0x16A}, {0, -0x200}, {0x16A, -0x16A},
    };
    for (int i = 0; i < n; i++) {
        const s16* d = n == 12 ? dir12[(i + offset) % 12] : dir8[(i + offset) % 8];
        int k = core.phase ? 4 : 3;
        bullet_fire(TO_FP(core.x), TO_FP(core.y), d[0] * k / 4, d[1] * k / 4);
    }
    sfx_play(SFX_SHOT);
}

static void set_state(int s) {
    core.state = (u8)s;
    core.t = 0;
}

static void hit_check(void) {
    int ax0, ay0, ax1, ay1, dmg, kbx, kby, stop;
    if (!player_attack_box(&ax0, &ay0, &ax1, &ay1, &dmg, &kbx, &kby, &stop)) return;
    if (core.last_hit == player.attack_id) return;
    int x0 = core.x - CW / 2, y0 = core.y - CH / 2;
    if (!(ax0 < x0 + CW && x0 < ax1 && ay0 < y0 + CH && y0 < ay1)) return;
    core.last_hit = player.attack_id;
    int hx = (MAX(ax0, x0) + MIN(ax1, x0 + CW)) / 2;
    int hy = (MAX(ay0, y0) + MIN(ay1, y0 + CH)) / 2;
    if (core.state != CS_OPEN) {
        /* 装甲に弾かれる */
        fx_spawn(FX_SPARK, hx - 4, hy - 4, 0);
        sfx_play(SFX_CHECK);
        game.hitstop = 2;
        return;
    }
    core.hp -= dmg;
    core.flash = 8;
    game.hitstop = (u16)(stop + 1);
    player.hit_any = 1;
    game_add_chain(1);
    sfx_play(SFX_HIT);
    fx_spawn(FX_SPARK, hx - 4, hy - 4 + rng_range(9) - 4, 0);
    if (stop >= 6) {
        shake_t = 6;
        shake_amp = 2;
        screen_flash(2);
    }
    if (core.phase == 0 && core.hp <= core.hp_max / 2) {
        core.phase = 1;
        fx_spawn(FX_BOOM, core.x - 8, core.y - 8, 0);
        sfx_play(SFX_BOOM);
        screen_flash(4);
    }
    if (core.hp <= 0) {
        core.hp = 0;
        set_state(CS_DEAD);
        bullets_clear();
        music_stop();
        screen_flash(8);
        game_add_score(10000);
        game.kills++;
    }
}

void core_update(void) {
    if (core.state == CS_NONE) return;
    core.t++;
    if (core.flash) core.flash--;

    /* 闘技場に閉じ込める */
    if (boss_lock_x >= 0 && core.state != CS_DEAD && FROM_FP(player.b.x) < boss_lock_x + 2) {
        player.b.x = TO_FP(boss_lock_x + 2);
        if (player.b.vx < 0) player.b.vx = 0;
    }

    /* 浮遊: 閉じている間は高く左右にも漂う、開くと低い位置へ */
    s32 ty = core.top_y, tx = core.home_x;
    if (core.state == CS_OPENING || core.state == CS_OPEN) ty = core.low_y;
    if (core.state == CS_CLOSED) tx = core.home_x - 40 + ((core.cycle & 1) ? 0 : -50);
    if (core.state != CS_INTRO && core.state != CS_DEAD) {
        core.x += (tx - core.x) / 16;
        core.y += (ty - core.y) / 10;
        if (core.state == CS_CLOSED) core.y += ((core.t >> 3) & 1) ? 1 : 0;
    }

    switch (core.state) {
    case CS_INTRO:
        if (core.y < core.top_y) core.y += 1;
        if (core.t == 1) text_put_center(8, "! FINAL WARNING !", BGPAL_HUD_Y);
        if (core.t == 60) {
            shake_t = 20;
            shake_amp = 3;
            sfx_play(SFX_BOOM);
        }
        if (core.t > 140) {
            text_put(0, 8, "                              ", BGPAL_HUD);
            set_state(CS_CLOSED);
        }
        return;

    case CS_CLOSED: {
        int ring_every = core.phase ? 45 : 60;
        if (core.t % ring_every == 20) ring_shot(core.phase ? 12 : 8, core.t / ring_every);
        /* 第2形態: レーザー予告 → 高速弾列 */
        if (core.phase && (core.t == 40 || core.t == 120)) {
            core.laser_y = (s16)(pcy() - 2);
            core.laser_t = 40;
        }
        if (core.phase && core.t == 100 && rng_range(2) == 0) item_spawn(OBJ_HEAL, core.x, core.y + 20);
        if (core.t > (u16)(core.phase ? 170 : 190)) set_state(CS_OPENING);
        break;
    }
    case CS_OPENING:
        if (core.t > 24) set_state(CS_OPEN);
        break;
    case CS_OPEN:
        if (core.t == 30 || core.t == 70 || (core.phase && core.t == 100)) {
            /* 狙い撃ち */
            int dx = pcx() - core.x, dy = pcy() - core.y;
            int adx = ABS(dx) + ABS(dy) + 1;
            s32 vx = dx * 0x200 / adx, vy = dy * 0x200 / adx;
            bullet_fire(TO_FP(core.x), TO_FP(core.y), vx, vy);
            bullet_fire(TO_FP(core.x), TO_FP(core.y), vx + vy / 4, vy - vx / 4);
            bullet_fire(TO_FP(core.x), TO_FP(core.y), vx - vy / 4, vy + vx / 4);
            sfx_play(SFX_SHOT);
        }
        /* 第2形態は開いている間もリング弾 */
        if (core.phase && core.t == 55) ring_shot(8, core.cycle);
        if (core.t > (u16)(core.phase ? 110 : 140)) set_state(CS_CLOSING);
        break;
    case CS_CLOSING:
        if (core.t > 20) {
            core.cycle++;
            set_state(CS_CLOSED);
        }
        break;
    case CS_DEAD:
        if ((core.t & 5) == 0 && core.t < 140) {
            fx_spawn(FX_BOOM, core.x - 8 + rng_range(48) - 24, core.y - 8 + rng_range(48) - 24, 0);
            fx_debris(core.x, core.y, 2);
            sfx_play(SFX_BOOM);
            shake_t = 6;
            shake_amp = 4;
            if ((core.t & 31) == 0) screen_flash(4);
        }
        if (core.t == 160) {
            core.state = CS_NONE;
            boss_lock_x = -1;
            game_boss_defeated(core.x, core.y);
        }
        return;
    }

    /* レーザー: 予告 (火花が点滅) → 左向きに高速弾列 */
    if (core.laser_y >= 0) {
        if (core.laser_t > 0) {
            core.laser_t--;
            if ((core.laser_t & 3) == 0)
                fx_spawn(FX_SPARK, boss_lock_x + 8 + rng_range(SCREEN_W - 16), core.laser_y - 4, 0);
            if (core.laser_t == 0) {
                for (int i = 0; i < 6; i++) bullet_fire(TO_FP(core.x - 20 - i * 10), TO_FP(core.laser_y), -0x480, 0);
                sfx_play(SFX_SLASH3);
                core.laser_y = -1;
            }
        }
    }

    hit_check();
    /* 接触ダメージ */
    if (core.state != CS_DEAD) {
        int x0 = core.x - CW / 2 + 4, y0 = core.y - CH / 2 + 4;
        int px = FROM_FP(player.b.x), py = FROM_FP(player.b.y);
        if (px < x0 + CW - 8 && x0 < px + player.b.w && py < y0 + CH - 8 && y0 < py + player.b.h)
            player_damage(2, core.x);
    }
}

void core_draw(void) {
    if (core.state == CS_NONE) return;
    if (core.state == CS_DEAD && (core.t & 2)) return;
    int spr = SPR_CORE_CLOSED;
    switch (core.state) {
    case CS_OPENING: spr = core.t < 12 ? SPR_CORE_HALF : SPR_CORE_OPEN; break;
    case CS_OPEN: spr = (core.t & 8) ? SPR_CORE_GLOW : SPR_CORE_OPEN; break;
    case CS_CLOSING: spr = core.t < 10 ? SPR_CORE_OPEN : SPR_CORE_HALF; break;
    case CS_DEAD: spr = SPR_CORE_OPEN; break;
    }
    int pal = core.flash ? ((core.flash & 2) ? OBJPAL_FLASH : OBJPAL_NORMAL) : (core.phase ? OBJPAL_ALT : OBJPAL_NORMAL);
    sprite_request_dynamic(spr);
    oam_put(spr, core.x - 32 - cam_x, core.y - 32 - cam_y, 0, pal, 2);
}
