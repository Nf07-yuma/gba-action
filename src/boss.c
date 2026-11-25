/* ボス「GUARDIAN」: 射撃 / 突進 / ジャンプ叩きつけ の 3 パターン、HP 半分で第 2 形態 */
#include "game.h"

enum { BS_NONE, BS_INTRO, BS_IDLE, BS_SHOOT, BS_CHARGE, BS_STUN, BS_JUMP, BS_DEAD };

typedef struct {
    u8 state;
    s8 dir;
    u8 flash;
    u8 last_hit;
    u8 phase;
    u8 next;
    u16 t;
    s16 hp, hp_max;
    Body b;
    s32 target_vx;
} Boss;

static Boss boss;
s32 boss_lock_x = -1;   /* カメラ固定位置 (-1 で固定なし) */
static s16 hud_last = -1;

#define BW 30
#define BH 44
#define BOSS_HP 40

static u8 kind; /* 0=GUARDIAN, 1=OVERLORD CORE */
int boss_active(void) { return kind ? core_active() : boss.state != BS_NONE; }
int boss_hp(void) { return kind ? core_hp() : (boss.state == BS_NONE ? 0 : boss.hp); }

void boss_reset(void) {
    kind = 0;
    core_reset();
    memset(&boss, 0, sizeof(boss));
    boss_lock_x = -1;
    hud_last = -1;
}

static int bcx(void) { return FROM_FP(boss.b.x) + BW / 2; }
static int bcy(void) { return FROM_FP(boss.b.y) + BH / 2; }
static int pcx(void) { return FROM_FP(player.b.x) + player.b.w / 2; }
int boss_state_id(void) { return kind ? 10 + core_state_id() : boss.state; }
int boss_x(void) { return kind ? core_x() : bcx(); }

/* ステージ末尾に到達したらボス戦を開始 */
int boss_check_start(int spawn_cx, int spawn_cy, int param) {
    if (boss.state != BS_NONE || core_active()) return 0;
    s32 lock = level.px_w - SCREEN_W;
    if (cam_x < lock - 4 || pcx() < lock + 24) return 0;
    memset(&boss, 0, sizeof(boss));
    if (param == 1) {
        kind = 1;
        boss_lock_x = lock;
        hud_last = -1;
        core_start(spawn_cx, spawn_cy);
        music_play(SONG_LASTBOSS);
        return 1;
    }
    kind = 0;
    boss.state = BS_INTRO;
    boss.hp = boss.hp_max = BOSS_HP;
    boss.dir = -1;
    boss.b.w = BW;
    boss.b.h = BH;
    boss.b.x = TO_FP(spawn_cx * CELL - BW / 2);
    /* 天井の直下から降ってくる */
    int top = 0;
    while (top < level.h && level_solid(spawn_cx, top)) top++;
    boss.b.y = TO_FP(top * CELL - BH / 2);
    boss_lock_x = lock;
    music_play(SONG_BOSS);
    text_put_center(8, "! WARNING !", BGPAL_HUD_Y);
    (void)spawn_cy;
    return 1;
}

static void face_player(void) { boss.dir = pcx() < bcx() ? -1 : 1; }

static void choose_next(void) {
    static const u8 order[2][6] = {
        {BS_SHOOT, BS_CHARGE, BS_JUMP, BS_SHOOT, BS_JUMP, BS_CHARGE},
        {BS_CHARGE, BS_JUMP, BS_SHOOT, BS_JUMP, BS_CHARGE, BS_SHOOT},
    };
    boss.state = order[boss.phase][boss.next % 6];
    boss.next++;
    boss.t = 0;
    face_player();
}

/* 闘技場の左右端を壁として扱う移動 */
static void boss_move(void) {
    body_move(&boss.b);
    if (boss_lock_x >= 0) {
        s32 l = TO_FP(boss_lock_x + 4), r = TO_FP(boss_lock_x + SCREEN_W - 4 - BW);
        if (boss.b.x < l) {
            boss.b.x = l;
            boss.b.hit_wall = 1;
            boss.b.vx = 0;
        } else if (boss.b.x > r) {
            boss.b.x = r;
            boss.b.hit_wall = 1;
            boss.b.vx = 0;
        }
    }
}

static void gravity(void) {
    boss.b.vy += 0x50;
    if (boss.b.vy > 0x600) boss.b.vy = 0x600;
}

static void shoot_at_player(int spread) {
    int mx = bcx() + boss.dir * 26, my = FROM_FP(boss.b.y) + 16;
    int dx = pcx() - mx;
    int dy = FROM_FP(player.b.y) + player.b.h / 2 - my;
    s32 vx = boss.dir * 0x200;
    s32 vy = CLAMP(dy * 0x200 / (ABS(dx) + 1), -0x180, 0x180);
    bullet_fire(TO_FP(mx), TO_FP(my), vx, vy);
    if (spread) {
        bullet_fire(TO_FP(mx), TO_FP(my), vx, vy - 0xA0);
        bullet_fire(TO_FP(mx), TO_FP(my), vx, vy + 0xA0);
    }
    sfx_play(SFX_SHOT);
}

static u8 boss_last_wave;
static void boss_apply_hit(int ax0, int ay0, int ax1, int ay1, int dmg, int stop);

static void boss_hit_check(void) {
    int ax0, ay0, ax1, ay1, dmg, kbx, kby, stop;
    int x0 = FROM_FP(boss.b.x), y0 = FROM_FP(boss.b.y);
    for (int k = 0; k < MAX_PWAVE && boss.state != BS_DEAD; k++) {
        u8 wid;
        if (player_wave_box(k, &ax0, &ay0, &ax1, &ay1, &wid) && wid != boss_last_wave &&
            ax0 < x0 + BW && x0 < ax1 && ay0 < y0 + BH && y0 < ay1) {
            boss_last_wave = wid;
            boss_apply_hit(ax0, ay0, ax1, ay1, 3, 4);
        }
    }
    if (boss.state == BS_DEAD) return;
    if (!player_attack_box(&ax0, &ay0, &ax1, &ay1, &dmg, &kbx, &kby, &stop)) return;
    if (boss.last_hit == player.attack_id) return;
    if (!(ax0 < x0 + BW && x0 < ax1 && ay0 < y0 + BH && y0 < ay1)) return;
    boss.last_hit = player.attack_id;
    boss_apply_hit(ax0, ay0, ax1, ay1, dmg, stop);
}

static void boss_apply_hit(int ax0, int ay0, int ax1, int ay1, int dmg, int stop) {
    int x0 = FROM_FP(boss.b.x), y0 = FROM_FP(boss.b.y);
    if (boss.state == BS_STUN) dmg += 1;  /* 気絶中は追加ダメージ */
    boss.hp -= dmg;
    boss.flash = 8;
    game.hitstop = (u16)(stop + 1);
    player.hit_any = 1;
    game_add_chain(1);
    sfx_play(SFX_HIT);
    int hx = (MAX(ax0, x0) + MIN(ax1, x0 + BW)) / 2;
    fx_spawn(FX_SPARK, hx - 4, (MAX(ay0, y0) + MIN(ay1, y0 + BH)) / 2 - 4 + rng_range(9) - 4, 0);
    if (stop >= 6) {
        shake_t = 6;
        shake_amp = 2;
    }
    if (boss.phase == 0 && boss.hp <= boss.hp_max / 2) {
        boss.phase = 1;
        fx_spawn(FX_BOOM, bcx() - 8, bcy() - 20, 0);
        sfx_play(SFX_BOOM);
    }
    if (boss.hp <= 0) {
        boss.hp = 0;
        boss.state = BS_DEAD;
        boss.t = 0;
        bullets_clear();
        music_stop();
        screen_flash(6);
        game_add_score(5000);
        game.kills++;
        /* とどめの一撃: 一瞬止めてからスローモーション */
        game.hitstop = 16;
        game.slowmo = 90;
    }
}

void boss_update(void) {
    if (kind) {
        core_update();
        return;
    }
    if (boss.state == BS_NONE) return;
    boss.t++;
    if (boss.flash) boss.flash--;

    /* プレイヤーを闘技場に閉じ込める */
    if (boss_lock_x >= 0 && boss.state != BS_DEAD) {
        if (FROM_FP(player.b.x) < boss_lock_x + 2) {
            player.b.x = TO_FP(boss_lock_x + 2);
            if (player.b.vx < 0) player.b.vx = 0;
        }
    }

    switch (boss.state) {
    case BS_INTRO:
        gravity();
        boss_move();
        if (boss.b.on_ground && boss.t < 200 && boss.t > 2 && boss.b.vy == 0 && !(boss.flash)) {
            if (boss.t < 120) {
                if (boss.next == 0) {
                    shake_t = 16;
                    shake_amp = 3;
                    sfx_play(SFX_BOOM);
                    boss.next = 1;
                }
            }
        }
        if (boss.t > 110) {
            text_put(0, 8, "                              ", BGPAL_HUD);
            boss.next = 0;
            boss.state = BS_IDLE;
            boss.t = 0;
        }
        break;

    case BS_IDLE: {
        face_player();
        int dist = ABS(pcx() - bcx());
        boss.b.vx = dist > 48 ? boss.dir * (boss.phase ? 0xA0 : 0x70) : 0;
        gravity();
        boss_move();
        if (boss.t > (u16)(boss.phase ? 30 : 55)) choose_next();
        break;
    }

    case BS_SHOOT: {
        boss.b.vx = 0;
        gravity();
        boss_move();
        int n = boss.phase ? 4 : 3;
        for (int i = 0; i < n; i++)
            if (boss.t == (u16)(20 + i * 16)) shoot_at_player(boss.phase && (i & 1));
        if (boss.t > (u16)(30 + n * 16)) {
            boss.state = BS_IDLE;
            boss.t = 0;
        }
        break;
    }

    case BS_CHARGE:
        gravity();
        if (boss.t < 36) {
            boss.b.vx = 0;
            if (boss.t & 4) boss.flash = 1;
        } else {
            boss.b.vx = boss.dir * (boss.phase ? 0x480 : 0x3A0);
            if ((boss.t & 3) == 0) fx_spawn(FX_DUST, bcx() - boss.dir * 14 - 4, FROM_FP(boss.b.y) + BH - 8, boss.dir > 0);
        }
        boss_move();
        if (boss.b.hit_wall && boss.t >= 36) {
            boss.state = BS_STUN;
            boss.t = 0;
            boss.b.vx = -boss.dir * 0x100;
            boss.b.vy = -0x200;
            shake_t = 18;
            shake_amp = 4;
            sfx_play(SFX_BOOM);
        }
        if (boss.t > 200) {
            boss.state = BS_IDLE;
            boss.t = 0;
        }
        break;

    case BS_STUN:
        gravity();
        if (boss.b.on_ground) boss.b.vx = 0;
        boss_move();
        if ((boss.t & 15) == 0) fx_spawn(FX_SPARK, bcx() - 4 + rng_range(16) - 8, FROM_FP(boss.b.y) + rng_range(10), 0);
        if (boss.t > (u16)(boss.phase ? 70 : 100)) {
            boss.state = BS_IDLE;
            boss.t = 0;
        }
        break;

    case BS_JUMP:
        if (boss.t < 14) {
            boss.b.vx = 0;
        } else if (boss.t == 14) {
            boss.b.vy = -0x6C0;
            int dx = pcx() - bcx();
            boss.target_vx = CLAMP(dx * 256 / 44, -0x300, 0x300);
            boss.b.vx = boss.target_vx;
        }
        gravity();
        boss_move();
        if (boss.t > 16 && boss.b.on_ground) {
            /* 着地: 衝撃波 */
            int gy = FROM_FP(boss.b.y) + BH;
            wave_fire(TO_FP(bcx() - 12), TO_FP(gy - 8), -(boss.phase ? 0x2C0 : 0x220));
            wave_fire(TO_FP(bcx() + 12), TO_FP(gy - 8), (boss.phase ? 0x2C0 : 0x220));
            shake_t = 14;
            shake_amp = 3;
            sfx_play(SFX_BOOM);
            fx_spawn(FX_DUST, bcx() - 20, gy - 8, 0);
            fx_spawn(FX_DUST, bcx() + 12, gy - 8, 1);
            boss.b.vx = 0;
            boss.state = BS_IDLE;
            boss.t = 0;
        }
        break;

    case BS_DEAD:
        boss.b.vx = 0;
        if ((boss.t & 7) == 0 && boss.t < 110) {
            fx_spawn(FX_BOOM, FROM_FP(boss.b.x) + rng_range(BW + 16) - 16, FROM_FP(boss.b.y) + rng_range(BH) - 8, 0);
            fx_debris(bcx(), bcy(), 2);
            if ((boss.t & 31) == 0) screen_flash(3);
            sfx_play(SFX_BOOM);
            shake_t = 6;
            shake_amp = 3;
        }
        if (boss.t == 130) {
            boss.state = BS_NONE;
            boss_lock_x = -1;
            game_boss_defeated(bcx(), FROM_FP(boss.b.y) + BH);
        }
        return;
    }

    boss_hit_check();
    /* 接触ダメージ (気絶中は無害) */
    if (boss.state != BS_STUN && boss.state != BS_DEAD && boss.state != BS_NONE && boss.state != BS_INTRO) {
        int x0 = FROM_FP(boss.b.x) + 2, y0 = FROM_FP(boss.b.y) + 6;
        int px = FROM_FP(player.b.x), py = FROM_FP(player.b.y);
        if (px < x0 + BW - 4 && x0 < px + player.b.w && py < y0 + BH - 6 && y0 < py + player.b.h)
            player_damage(boss.state == BS_CHARGE ? 3 : 2, bcx());
    }
}

void boss_draw(void) {
    if (kind) {
        core_draw();
        return;
    }
    if (boss.state == BS_NONE) return;
    if (boss.state == BS_DEAD && (boss.t & 2)) return;
    int spr = SPR_BOSS_IDLE0 + ((boss.t >> 4) & 1);
    switch (boss.state) {
    case BS_SHOOT: {
        int n = boss.phase ? 4 : 3;
        spr = SPR_BOSS_IDLE0;
        for (int i = 0; i < n; i++)
            if (boss.t >= (u16)(20 + i * 16) && boss.t < (u16)(26 + i * 16)) spr = SPR_BOSS_SHOOT;
        if (boss.t < 20 && (boss.t & 4)) spr = SPR_BOSS_CHARGE;
        break;
    }
    case BS_CHARGE: spr = SPR_BOSS_CHARGE; break;
    case BS_JUMP: spr = boss.b.on_ground && boss.t < 14 ? SPR_BOSS_CHARGE : SPR_BOSS_JUMP; break;
    case BS_STUN: spr = SPR_BOSS_IDLE1; break;
    case BS_INTRO: spr = boss.b.on_ground ? SPR_BOSS_IDLE0 : SPR_BOSS_JUMP; break;
    }
    int pal = boss.flash ? ((boss.flash & 2) || boss.state == BS_CHARGE ? OBJPAL_FLASH : OBJPAL_NORMAL)
                         : (boss.phase ? OBJPAL_ALT : OBJPAL_NORMAL);
    int sx = bcx() - 32 - cam_x + (boss.dir > 0 ? 2 : -2);
    int sy = FROM_FP(boss.b.y) + BH - 63 - cam_y;
    sprite_request_dynamic(spr);
    oam_put(spr, sx, sy, boss.dir > 0, pal, 2);
}

/* 画面下部のボス HP ゲージ */
void boss_hud(void) {
    s16 hp = boss.state == BS_NONE || boss.state == BS_DEAD ? -1 : boss.hp;
    if (boss.state == BS_INTRO) hp = boss.hp_max;
    s16 hpmax = boss.hp_max;
    if (kind) {
        hp = core_active() && !core_dead() ? (s16)core_hp() : -1;
        hpmax = (s16)core_hp_max();
    }
    if (hp == hud_last) return;
    u16* m = SCREENBLOCK(SBB_HUD);
    if (hp < 0) {
        for (int x = 0; x < 30; x++) m[19 * 32 + x] = 0;
    } else {
        text_put(1, 19, kind ? "OVERLORD" : "GUARDIAN", BGPAL_HUD_Y);
        int cells = 18;
        int fill = (hp * cells + hpmax - 1) / hpmax;
        for (int i = 0; i < cells; i++)
            m[19 * 32 + 10 + i] = (u16)((i < fill ? HUD_TILE_BOSS_FULL : HUD_TILE_BOSS_EMPTY) | (BGPAL_HUD << 12));
    }
    hud_last = hp;
}
