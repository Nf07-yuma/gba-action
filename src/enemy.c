/* 敵・ステージ上のオブジェクト(チェックポイント/ゴール/回復)・弾 */
#include "game.h"

typedef struct {
    u8 active, type, state;
    s8 dir;
    Body b;
    s16 hp;
    u8 hitstun, flash;
    u8 last_hit;
    u8 cool;
    u16 t;
    s32 ax, ay;    /* 出現位置 (ピクセル) */
    s16 ldx, ldy;  /* 足場の今フレームの移動量 */
    u8 phase;      /* 足場の位相 (0-63) */
    u8 last_wave;  /* 最後に当たった斬撃波 */
    s16 obj;       /* レベルオブジェクト番号 (-1 = ドロップ品) */
} Ent;

#define MAX_ENTS 20
#define MAX_OBJS 256
static Ent ents[MAX_ENTS];
static u8 obj_flag[MAX_OBJS];
static s8 standing_lift = -1;
#define OF_SPAWNED 1
#define OF_DEAD    2
#define OF_LIT     4

/* 64 分割のサイン (振幅 64) */
static const s8 sin64[64] = {
    0, 6, 12, 18, 24, 30, 35, 40, 45, 49, 53, 56, 59, 61, 62, 63,
    64, 63, 62, 61, 59, 56, 53, 49, 45, 40, 35, 30, 24, 18, 12, 6,
    0, -6, -12, -18, -24, -30, -35, -40, -45, -49, -53, -56, -59, -61, -62, -63,
    -64, -63, -62, -61, -59, -56, -53, -49, -45, -40, -35, -30, -24, -18, -12, -6,
};

static int is_enemy(int type) {
    return type == OBJ_CRAWLER || type == OBJ_DRONE || type == OBJ_TURRET || type == OBJ_JUMPER || type == OBJ_SHIELD ||
           type == OBJ_MINE || type == OBJ_LASER || type == OBJ_BOMBER;
}

void enemies_reset(int full) {
    memset(ents, 0, sizeof(ents));
    standing_lift = -1;
    for (int i = 0; i < MAX_OBJS; i++) obj_flag[i] = full ? 0 : (obj_flag[i] & OF_LIT);
}

static Ent* ent_alloc(void) {
    for (int i = 0; i < MAX_ENTS; i++)
        if (!ents[i].active) {
            memset(&ents[i], 0, sizeof(Ent));
            ents[i].active = 1;
            return &ents[i];
        }
    return 0;
}

static void ent_init(Ent* e, int type, int px, int py) {
    /* px,py: セル左下を基準とした足元位置 */
    e->type = (u8)type;
    e->dir = -1;
    e->b.oneway = 1;
    int w = 12, h = 12;
    switch (type) {
    case OBJ_CRAWLER: w = 14; h = 10; e->hp = 3; break;
    case OBJ_DRONE:   w = 12; h = 12; e->hp = 2; break;
    case OBJ_TURRET:  w = 14; h = 12; e->hp = 4; break;
    case OBJ_JUMPER:  w = 10; h = 11; e->hp = 2; e->cool = 30; break;
    case OBJ_SHIELD:  w = 10; h = 15; e->hp = 4; break;
    case OBJ_MINE:    w = 12; h = 12; e->hp = 1; break;
    case OBJ_LASER:   w = 12; h = 12; e->hp = 3; break;
    case OBJ_BOMBER:  w = 14; h = 10; e->hp = 3; e->cool = 40; break;
    case OBJ_CHECKPOINT: w = 8; h = 16; break;
    case OBJ_GOAL:    w = 18; h = 30; break;
    case OBJ_HEAL:    w = 8; h = 8; break;
    case OBJ_LIFT_H:
    case OBJ_LIFT_V:  w = 32; h = 6; break;
    }
    e->b.w = (s16)w;
    e->b.h = (s16)h;
    e->b.x = TO_FP(px - w / 2);
    e->b.y = TO_FP(py - h);
    if (type == OBJ_DRONE || type == OBJ_MINE || type == OBJ_BOMBER) e->b.y = TO_FP(py - h - 4);
    /* 足場の上面は配置したセルの下端 (= 隣の地面と同じ高さ) */
    if (type == OBJ_LIFT_H || type == OBJ_LIFT_V) e->b.y = TO_FP(py);
    e->ax = px;
    e->ay = FROM_FP(e->b.y);
}

void item_spawn(int type, s32 x, s32 y) {
    Ent* e = ent_alloc();
    if (!e) return;
    ent_init(e, type, x, y);
    e->obj = -1;
    e->b.vy = -0x300;
}

void items_clear(void) {}
void items_update(void) {}
void items_draw(void) {}

int enemies_alive(void) {
    int n = 0;
    for (int i = 0; i < MAX_ENTS; i++)
        if (ents[i].active && is_enemy(ents[i].type)) n++;
    return n;
}

void enemies_spawn_check(void) {
    const LevelDef* d = level.def;
    int l = (cam_x - 72) >> 4, r = (cam_x + SCREEN_W + 72) >> 4;
    int el = (cam_x - 24) >> 4, er = (cam_x + SCREEN_W + 24) >> 4; /* 敵は画面のすぐ外で出現 */
    for (int i = 0; i < d->nobjs && i < MAX_OBJS; i++) {
        const LevelObj* o = &d->objs[i];
        if (o->x < l) continue;
        if (o->x > r) break;
        if (obj_flag[i] & (OF_SPAWNED | OF_DEAD)) continue;
        if (o->type != OBJ_LIFT_H && o->type != OBJ_LIFT_V && (o->x < el || o->x > er)) continue;
        if (o->type == OBJ_BOSS) {
            if (boss_check_start(o->x, o->y, o->param)) obj_flag[i] |= OF_SPAWNED;
            continue;
        }
        Ent* e = ent_alloc();
        if (!e) return;
        ent_init(e, o->type, o->x * CELL + CELL / 2, (o->y + 1) * CELL);
        e->obj = (s16)i;
        e->phase = o->param;
        /* 足場は画面外でも時間に同期して動く (再出現しても位置が飛ばない) */
        if (o->type == OBJ_LIFT_H || o->type == OBJ_LIFT_V) e->t = (u16)game.frame;
        obj_flag[i] |= OF_SPAWNED;
    }
}

static void ent_free(Ent* e, int dead) {
    if (e->obj >= 0) {
        obj_flag[e->obj] &= ~OF_SPAWNED;
        if (dead) obj_flag[e->obj] |= OF_DEAD;
    }
    e->active = 0;
}

static int px_center(const Ent* e) { return FROM_FP(e->b.x) + e->b.w / 2; }
static int py_center(const Ent* e) { return FROM_FP(e->b.y) + e->b.h / 2; }
static int player_cx(void) { return FROM_FP(player.b.x) + player.b.w / 2; }
static int player_cy(void) { return FROM_FP(player.b.y) + player.b.h / 2; }

static int overlap(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1) {
    return ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
}

static int body_overlap(const Body* a, const Body* b) {
    int ax = FROM_FP(a->x), ay = FROM_FP(a->y), bx = FROM_FP(b->x), by = FROM_FP(b->y);
    return overlap(ax, ay, ax + a->w, ay + a->h, bx, by, bx + b->w, by + b->h);
}

static void enemy_die(Ent* e) {
    int cx = px_center(e), cy = py_center(e);
    fx_spawn(FX_BOOM, cx - 8, cy - 8, 0);
    fx_debris(cx, cy, 5);
    sfx_play(SFX_BOOM);
    shake_t = 6;
    shake_amp = 2;
    game.kills++;
    game_add_score(100);
    /* 回復ドロップ (HARD はピンチ時にまれに出るだけ) */
    if (game.hard ? (player.hp <= 2 && rng_range(4) == 0) : (rng_range(6) == 0 || (player.hp <= 2 && rng_range(2) == 0)))
        item_spawn(OBJ_HEAL, cx, cy);
    ent_free(e, 1);
}

static void enemy_hit(Ent* e, int dmg, int kbx, int kby, int stop, int pierce) {
    int from_left = player_cx() < px_center(e);
    int dir = from_left ? 1 : -1;
    /* シールド兵: 正面からの通常攻撃は弾く (フィニッシュ/空中/背面は通る) */
    if (!pierce && e->type == OBJ_SHIELD && player.combo < 2 && e->dir == -dir && player.combo != 3) {
        fx_spawn(FX_SPARK, px_center(e) - 4 - dir * 6, py_center(e) - 4, 0);
        sfx_play(SFX_GUARD);
        e->state = 10; /* ガード演出 */
        player.b.vx = -dir * 0x200;
        game.hitstop = 2;
        return;
    }
    e->hp -= dmg;
    e->flash = 8;
    e->hitstun = 18;
    if (e->type != OBJ_TURRET && e->type != OBJ_MINE && e->type != OBJ_LASER) {
        e->b.vx = dir * kbx;
        e->b.vy = kby;
        if (e->type == OBJ_DRONE || e->type == OBJ_BOMBER) e->b.vy = kby / 2;
    }
    game.hitstop = (u16)stop;
    player.hit_any = 1;
    game_add_chain(1);
    sfx_play(SFX_HIT);
    fx_spawn(FX_SPARK, (px_center(e) + player_cx()) / 2 - 4 + dir * 4, py_center(e) - 4 + rng_range(7) - 3, 0);
    if (stop >= 6) {
        shake_t = 6;
        shake_amp = 2;
        screen_flash(2);
    }
    if (e->hp <= 0) enemy_die(e);
}

/* 浮遊機雷: 近づくと点滅し、時間切れで全方位に破片弾。点滅中に斬れば安全に壊せる */
static void mine_explode(Ent* e) {
    static const s16 d8[8][2] = {{0x180, 0}, {0x110, 0x110}, {0, 0x180}, {-0x110, 0x110},
                                 {-0x180, 0}, {-0x110, -0x110}, {0, -0x180}, {0x110, -0x110}};
    int cx = px_center(e), cy = py_center(e);
    for (int i = 0; i < 8; i++) bullet_fire(TO_FP(cx), TO_FP(cy), d8[i][0], d8[i][1]);
    fx_spawn(FX_BOOM, cx - 8, cy - 8, 0);
    sfx_play(SFX_BOOM);
    shake_t = 6;
    shake_amp = 2;
    ent_free(e, 1);
}

void bomb_drop(s32 x, s32 y, s32 vx);

/* 爆撃ドローン: 一定の高さを保ったままプレイヤーの真上へ寄ってきて爆弾を落とす */
static void update_bomber(Ent* e) {
    int dx = player_cx() - px_center(e);
    if (e->hitstun) {
        e->b.vx = e->b.vx * 7 / 8;
        e->b.vy = e->b.vy * 7 / 8;
        e->b.x += e->b.vx;
        e->b.y += e->b.vy;
        return;
    }
    /* 横: プレイヤーへ向けて加速 (行き過ぎてから折り返すので真上で止まらない) */
    s32 want = dx > 0 ? 0x130 : -0x130;
    /* 出現地点から左右 112px までしか追わない (足場渡りの区間まで付いてこない) */
    int home = px_center(e) - e->ax;
    if ((home > 112 && want > 0) || (home < -112 && want < 0)) want = -want;
    if (e->b.vx < want) e->b.vx += 0x0C;
    else if (e->b.vx > want) e->b.vx -= 0x0C;
    e->dir = e->b.vx > 0 ? 1 : -1;
    /* 縦: 出現時の高さへ戻りつつゆっくり上下 */
    s32 ty = TO_FP(e->ay + sin64[(e->t * 2) & 63] / 16);
    e->b.y += (ty - e->b.y) / 16;
    e->b.x += e->b.vx;
    if (e->cool) e->cool--;
    int dy = player_cy() - py_center(e);
    if (!e->cool && ABS(dx) < 14 && dy > 8 && dy < 140) {
        bomb_drop(TO_FP(px_center(e)), TO_FP(FROM_FP(e->b.y) + e->b.h + 2), e->b.vx / 2);
        sfx_play(SFX_SHOT);
        e->cool = game.hard ? 50 : 70;
        e->state = 6; /* 投下の反動表示 */
    }
    if (e->state) e->state--;
}

static void update_mine(Ent* e) {
    int dx = player_cx() - px_center(e), dy = player_cy() - py_center(e);
    /* ふわふわ上下 */
    e->b.y = TO_FP(e->ay) + sin64[(e->t * 2) & 63] * TO_FP(4) / 64;
    if (e->state == 0) {
        if (ABS(dx) + ABS(dy) < 56) {
            e->state = 1;
            e->cool = 50;
            sfx_play(SFX_ALARM);
        }
    } else if (--e->cool == 0) {
        mine_explode(e);
    } else if (e->cool % 12 == 0) {
        sfx_play(SFX_ALARM);
    }
}

/* レーザー砲台: 待機 → 予告線 (0.7 秒) → 照射 (0.5 秒)。壁まで届く。斬れば壊せる */
#define LASER_CYCLE 170
#define LASER_WARN 60
#define LASER_FIRE 100
#define LASER_END 130
static int laser_dir(const Ent* e) { return e->phase ? 1 : -1; }
static int laser_len(const Ent* e) {
    int d = laser_dir(e);
    int cx = (px_center(e) >> 4) + d, cy = py_center(e) >> 4;
    int n = 0;
    while (n < 15 && !level_solid(cx + d * n, cy)) n++;
    return n * 16 + 8 - 6;
}

static void update_laser(Ent* e) {
    u16 c = e->t % LASER_CYCLE;
    if (c == LASER_WARN) sfx_play(SFX_ALARM);
    if (c == LASER_FIRE) {
        sfx_play(SFX_LASER);
        shake_t = 4;
        shake_amp = 1;
    }
    if (c >= LASER_FIRE && c < LASER_END && player.state != PS_DEAD) {
        int d = laser_dir(e), len = laser_len(e);
        int x0 = d > 0 ? px_center(e) + 6 : px_center(e) - 6 - len;
        int y0 = py_center(e) - 3;
        int px = FROM_FP(player.b.x), py = FROM_FP(player.b.y);
        if (overlap(x0, y0, x0 + len, y0 + 6, px + 1, py + 2, px + player.b.w - 1, py + player.b.h))
            player_damage(2, px_center(e));
    }
}

static void update_crawler(Ent* e) {
    if (!e->hitstun) {
        e->b.vx = e->dir * 0x60;
        /* 崖と壁で反転 */
        int fx = e->dir > 0 ? FROM_FP(e->b.x) + e->b.w + 1 : FROM_FP(e->b.x) - 2;
        int fy = FROM_FP(e->b.y) + e->b.h + 2;
        int below = level_cell(fx >> 4, fy >> 4);
        if (e->b.on_ground && below != CELL_SOLID && below != CELL_ONEWAY) e->dir = (s8)-e->dir;
        if (e->b.hit_wall) e->dir = (s8)-e->dir;
    } else {
        e->b.vx = e->b.vx * 7 / 8;
    }
    e->b.vy += 0x60;
    if (e->b.vy > 0x500) e->b.vy = 0x500;
    body_move(&e->b);
}

static void update_drone(Ent* e) {
    int dx = player_cx() - px_center(e);
    int dy = player_cy() - py_center(e);
    if (e->hitstun) {
        e->b.vx = e->b.vx * 7 / 8;
        e->b.vy = e->b.vy * 7 / 8;
        e->b.x += e->b.vx;
        e->b.y += e->b.vy;
        if (e->hitstun == 1) e->state = 2;
        return;
    }
    e->dir = dx < 0 ? -1 : 1;
    switch (e->state) {
    case 0: { /* 待機ホバリング */
        int tx = e->ax - e->b.w / 2 + sin64[(e->t >> 1) & 63] / 4;
        int ty = e->ay + sin64[(e->t * 3 >> 1) & 63] / 10;
        e->b.x += (TO_FP(tx) - e->b.x) / 8;
        e->b.y += (TO_FP(ty) - e->b.y) / 8;
        if (e->cool) e->cool--;
        if (!e->cool && ABS(dx) < 100 && dy > -16 && dy < 90) {
            e->state = 1;
            e->t = 0;
            e->b.vx = CLAMP(dx * 6, -0x220, 0x220);
            e->b.vy = CLAMP(dy * 8, -0x100, 0x2C0);
        }
        break;
    }
    case 1: /* 急降下 */
        e->b.x += e->b.vx;
        e->b.y += e->b.vy;
        e->b.vy -= 0x0C;
        if (e->t > 50 || level_solid(px_center(e) >> 4, py_center(e) >> 4)) {
            e->state = 2;
        }
        break;
    case 2: /* 元の位置へ戻る */
        e->b.x += (TO_FP(e->ax - e->b.w / 2) - e->b.x) / 16;
        e->b.y += (TO_FP(e->ay) - e->b.y) / 16;
        if (ABS(FROM_FP(e->b.y) - e->ay) < 3) {
            e->state = 0;
            e->cool = 70;
        }
        break;
    }
}

static void update_turret(Ent* e) {
    int dx = player_cx() - px_center(e);
    e->dir = dx < 0 ? -1 : 1;
    if (e->cool) e->cool--;
    if (!e->cool && ABS(dx) < 150 && !e->hitstun) {
        int dy = player_cy() - py_center(e);
        s32 vx = e->dir * 0x180;
        s32 vy = CLAMP(dy * 0x180 / (ABS(dx) + 1), -0x100, 0x100);
        bullet_fire(TO_FP(px_center(e) + e->dir * 8), TO_FP(py_center(e) - 3), vx, vy);
        e->cool = 110;
        e->state = 8; /* 発射フレーム表示 */
        sfx_play(SFX_SHOT);
    }
    if (e->state) e->state--;
    e->b.vy += 0x60;
    if (e->b.vy > 0x500) e->b.vy = 0x500;
    e->b.vx = 0;
    body_move(&e->b);
}

static void update_jumper(Ent* e) {
    int dx = player_cx() - px_center(e);
    if (e->b.on_ground && !e->hitstun) {
        e->b.vx = 0;
        e->dir = dx < 0 ? -1 : 1;
        if (e->cool) e->cool--;
        else if (ABS(dx) < 140) {
            e->b.vy = -0x480;
            e->b.vx = e->dir * 0x140;
            e->cool = 50;
        }
    }
    if (e->hitstun) e->b.vx = e->b.vx * 7 / 8;
    e->b.vy += 0x50;
    if (e->b.vy > 0x500) e->b.vy = 0x500;
    body_move(&e->b);
}

static void update_shield(Ent* e) {
    if (e->state) e->state--;
    int dx = player_cx() - px_center(e);
    if (!e->hitstun) {
        if (!(e->t & 31)) e->dir = dx < 0 ? -1 : 1;
        e->b.vx = (ABS(dx) < 120 && ABS(dx) > 12) ? e->dir * 0x48 : 0;
        int fx = e->dir > 0 ? FROM_FP(e->b.x) + e->b.w + 1 : FROM_FP(e->b.x) - 2;
        int fy = FROM_FP(e->b.y) + e->b.h + 2;
        int below = level_cell(fx >> 4, fy >> 4);
        if (e->b.on_ground && below != CELL_SOLID && below != CELL_ONEWAY) e->b.vx = 0;
    } else {
        e->b.vx = e->b.vx * 7 / 8;
    }
    e->b.vy += 0x60;
    if (e->b.vy > 0x500) e->b.vy = 0x500;
    body_move(&e->b);
}

/* ---- 動く足場 ---- */

static void update_lift(Ent* e) {
    int ph = (e->t >> 2) + e->phase;
    s32 ox = e->b.x, oy = e->b.y;
    if (e->type == OBJ_LIFT_H) {
        e->b.x = TO_FP(e->ax - e->b.w / 2) + sin64[ph & 63] * TO_FP(44) / 64;
    } else {
        e->b.y = TO_FP(e->ay - 32) + sin64[ph & 63] * TO_FP(32) / 64;
    }
    e->ldx = (s16)(e->b.x - ox);
    e->ldy = (s16)(e->b.y - oy);
}

/* プレイヤーと足場の接触: 乗っていれば運び、落下中に上面に触れたら着地 */
void lifts_apply(Body* b) {
    int prev = standing_lift;
    standing_lift = -1;
    for (int i = 0; i < MAX_ENTS; i++) {
        Ent* e = &ents[i];
        if (!e->active || (e->type != OBJ_LIFT_H && e->type != OBJ_LIFT_V)) continue;
        if (i == prev) {
            b->x += e->ldx;
            b->y += e->ldy;
        }
        int top = FROM_FP(e->b.y);
        int l = FROM_FP(e->b.x), r = l + e->b.w;
        int px = FROM_FP(b->x), bot = FROM_FP(b->y) + b->h;
        if (b->vy < 0 || px + b->w <= l + 1 || px >= r - 1) continue;
        if (bot >= top - 1 && bot <= top + 7) {
            b->y = TO_FP(top - b->h);
            b->vy = 0;
            b->on_ground = 1;
            standing_lift = (s8)i;
            return;
        }
    }
}

int lifts_debug(s32* xy, int max) {
    int n = 0;
    for (int i = 0; i < MAX_ENTS && n < max; i++) {
        Ent* e = &ents[i];
        if (!e->active || (e->type != OBJ_LIFT_H && e->type != OBJ_LIFT_V)) continue;
        xy[n * 2] = FROM_FP(e->b.x);
        xy[n * 2 + 1] = FROM_FP(e->b.y);
        n++;
    }
    return n;
}

static void update_prop(Ent* e) {
    switch (e->type) {
    case OBJ_LIFT_H:
    case OBJ_LIFT_V:
        update_lift(e);
        break;
    case OBJ_CHECKPOINT:
        if (!(obj_flag[e->obj] & OF_LIT) && body_overlap(&e->b, &player.b)) {
            obj_flag[e->obj] |= OF_LIT;
            game_set_checkpoint(px_center(e), FROM_FP(e->b.y) + e->b.h);
            sfx_play(SFX_CHECK);
            fx_spawn(FX_SPARK, px_center(e) - 4, FROM_FP(e->b.y) - 2, 0);
        }
        break;
    case OBJ_GOAL:
        /* ゲートの列を通過すれば高さに関係なく反応 (飛び越し対策) */
        if (player.state != PS_DEAD && player.state != PS_CLEAR) {
            int gx = FROM_FP(e->b.x), px = FROM_FP(player.b.x);
            if (px < gx + e->b.w && gx < px + player.b.w && FROM_FP(player.b.y) < FROM_FP(e->b.y) + e->b.h)
                game_stage_clear();
        }
        break;
    case OBJ_HEAL:
        e->b.vy += 0x40;
        if (e->b.vy > 0x400) e->b.vy = 0x400;
        body_move(&e->b);
        if (body_overlap(&e->b, &player.b) && player.state != PS_DEAD) {
            player.hp = MIN(player.hp_max, player.hp + 3);
            game.hud_dirty = 1;
            sfx_play(SFX_PICKUP);
            fx_spawn(FX_SPARK, px_center(e) - 4, py_center(e) - 4, 0);
            ent_free(e, 1);
            return;
        }
        if (e->obj < 0 && e->t > 600) ent_free(e, 0);
        break;
    }
}

void enemies_update(void) {
    int ax0, ay0, ax1, ay1, dmg, kbx, kby, stop;
    int atk = player_attack_box(&ax0, &ay0, &ax1, &ay1, &dmg, &kbx, &kby, &stop);
    for (int i = 0; i < MAX_ENTS; i++) {
        Ent* e = &ents[i];
        if (!e->active) continue;
        e->t++;
        /* 画面外に大きく離れたら消す (再出現可能) */
        int ex = FROM_FP(e->b.x);
        int margin = (e->type == OBJ_LIFT_H || e->type == OBJ_LIFT_V) ? 160 : 96;
        if (ex < cam_x - margin || ex > cam_x + SCREEN_W + margin || FROM_FP(e->b.y) > level.px_h + 32) {
            if (FROM_FP(e->b.y) > level.px_h + 32 && is_enemy(e->type)) {
                game.kills++;
                game_add_score(100);
                ent_free(e, 1);
            } else {
                ent_free(e, 0);
            }
            continue;
        }
        if (!is_enemy(e->type)) {
            update_prop(e);
            continue;
        }
        if (e->flash) e->flash--;
        if (e->hitstun) e->hitstun--;
        switch (e->type) {
        case OBJ_CRAWLER: update_crawler(e); break;
        case OBJ_DRONE:   update_drone(e); break;
        case OBJ_TURRET:  update_turret(e); break;
        case OBJ_JUMPER:  update_jumper(e); break;
        case OBJ_SHIELD:  update_shield(e); break;
        case OBJ_MINE:    update_mine(e); break;
        case OBJ_LASER:   update_laser(e); break;
        case OBJ_BOMBER:  update_bomber(e); break;
        }
        if (!e->active) continue;
        /* トゲに落ちたら壊れる */
        if (e->type != OBJ_DRONE && e->type != OBJ_MINE && e->type != OBJ_LASER && e->type != OBJ_BOMBER && body_touch_hazard(&e->b)) {
            enemy_die(e);
            continue;
        }
        /* プレイヤーの攻撃 */
        int bx = FROM_FP(e->b.x), by = FROM_FP(e->b.y);
        if (atk && e->last_hit != player.attack_id && overlap(ax0, ay0, ax1, ay1, bx, by, bx + e->b.w, by + e->b.h)) {
            e->last_hit = player.attack_id;
            enemy_hit(e, dmg, kbx, kby, stop, 0);
            if (!e->active) continue;
        }
        /* チャージ斬りの斬撃波 (盾を貫通) */
        for (int k = 0; k < MAX_PWAVE && e->active; k++) {
            int wx0, wy0, wx1, wy1;
            u8 wid;
            if (player_wave_box(k, &wx0, &wy0, &wx1, &wy1, &wid) && e->last_wave != wid &&
                overlap(wx0, wy0, wx1, wy1, bx, by, bx + e->b.w, by + e->b.h)) {
                e->last_wave = wid;
                enemy_hit(e, 3, 0x300, -0x200, 4, 1);
            }
        }
        if (!e->active) continue;
        /* 接触ダメージ (怯み中は無害)。機雷は触れると即爆発 */
        if (e->type == OBJ_MINE && body_overlap(&e->b, &player.b)) {
            player_damage(2, px_center(e));
            mine_explode(e);
            continue;
        }
        if (!e->hitstun && body_overlap(&e->b, &player.b)) {
            player_damage(1, px_center(e));
        }
    }
}

void enemies_draw(void) {
    for (int i = 0; i < MAX_ENTS; i++) {
        Ent* e = &ents[i];
        if (!e->active) continue;
        int cx = px_center(e) - cam_x;
        int bot = FROM_FP(e->b.y) + e->b.h - cam_y;
        int pal = e->flash ? ((e->flash & 2) ? OBJPAL_FLASH : OBJPAL_NORMAL) : OBJPAL_NORMAL;
        int flip = e->dir > 0;
        int spr;
        /* 被弾リアクション: 小刻みに震える */
        if (e->flash) cx += (e->flash & 2) ? 1 : -1;
        switch (e->type) {
        case OBJ_CRAWLER: {
            /* 歩行: 2 コマ + 上下動 */
            int ph = (e->t >> 3) & 3;
            spr = SPR_CRAWLER0 + (ph & 1);
            oam_put(spr, cx - 8, bot - 15 - (ph == 1 && !e->hitstun), flip, pal, 2);
            break;
        }
        case OBJ_DRONE: {
            /* 浮遊感: ゆっくり上下, 急降下中は前傾 (左右反転で向きを強調) */
            int bob = e->state == 1 ? 0 : (sin64[(e->t * 2) & 63] / 32);
            spr = SPR_DRONE0 + ((e->t >> 1) & 1);
            oam_put(spr, cx - 8, bot - 14 + bob, flip, pal, 2);
            break;
        }
        case OBJ_BOMBER:
            /* プロペラ 2 コマ, 投下の瞬間に機体が少し浮く */
            spr = SPR_BOMBER0 + ((e->t >> 1) & 1);
            oam_put(spr, cx - 8, bot - 13 - (e->state > 0), flip, pal, 2);
            break;
        case OBJ_TURRET:
            /* 発射時に砲身が反動で下がる */
            spr = e->state ? SPR_TURRET1 : SPR_TURRET0;
            oam_put(spr, cx - 8 + (e->state > 4 ? (flip ? -1 : 1) : 0), bot - 16, flip, pal, 2);
            break;
        case OBJ_JUMPER: {
            /* 跳ぶ直前にしゃがむ */
            int crouch = e->b.on_ground && e->cool > 0 && e->cool < 8;
            spr = e->b.on_ground ? SPR_JUMPER0 : SPR_JUMPER1;
            oam_put(spr, cx - 8, bot - 16 + crouch * 2, flip, pal, 2);
            break;
        }
        case OBJ_SHIELD:
            /* ガードした直後は盾が光る */
            spr = SPR_SHIELD0 + ((ABS(e->b.vx) > 0 && (e->t >> 3) & 1) ? 1 : 0);
            if (e->state && (e->state & 2)) pal = OBJPAL_FLASH;
            oam_put(spr, cx - 8 + (flip ? 2 : -2), bot - 16, flip, pal, 2);
            break;
        case OBJ_LASER: {
            u16 c = e->t % LASER_CYCLE;
            int d = laser_dir(e);
            oam_put(c >= LASER_WARN && c < LASER_END && (c & 4) ? SPR_LASER1 : SPR_LASER0, cx - 8, bot - 14, d > 0, pal, 2);
            if (c >= LASER_WARN && c < LASER_END) {
                int len = laser_len(e);
                int spr2 = c >= LASER_FIRE ? SPR_BEAM0 : SPR_BEAM1;
                if (c < LASER_FIRE && (c & 2)) break;
                int y = FROM_FP(e->b.y) + e->b.h / 2 - 4 - cam_y;
                for (int o = 0; o < len; o += 16) {
                    int x = d > 0 ? cx + 6 + o : cx - 6 - o - 16;
                    oam_put(spr2, x, y, 0, OBJPAL_NORMAL, 1);
                }
            }
            break;
        }
        case OBJ_MINE:
            spr = (e->state && (e->cool & (e->cool < 20 ? 2 : 4))) ? SPR_MINE1 : SPR_MINE0;
            oam_put(spr, cx - 8, bot - 14, 0, pal, 2);
            break;
        case OBJ_CHECKPOINT:
            spr = (obj_flag[e->obj] & OF_LIT) ? SPR_CHECKPOINT1 : SPR_CHECKPOINT0;
            oam_put(spr, cx - 8, bot - 16, 0, OBJPAL_NORMAL, 2);
            break;
        case OBJ_GOAL:
            oam_put(SPR_GATE0 + ((e->t >> 3) & 1), cx - 16, bot - 32, 0, OBJPAL_NORMAL, 2);
            break;
        case OBJ_LIFT_H:
        case OBJ_LIFT_V:
            oam_put(SPR_LIFT0 + ((e->t >> 3) & 1), FROM_FP(e->b.x) - cam_x, FROM_FP(e->b.y) - cam_y, 0, OBJPAL_NORMAL, 2);
            break;
        case OBJ_HEAL:
            if (e->obj < 0 && e->t > 480 && (e->t & 4)) break;
            oam_put(SPR_HEAL, cx - 4, bot - 8 - ((e->t >> 4) & 1), 0, OBJPAL_NORMAL, 1);
            break;
        }
    }
}

/* ---- 弾 ---- */
typedef struct {
    u8 active;
    u8 kind;  /* 0=弾, 1=衝撃波, 2=爆弾 (重力で落ち、着地で爆発) */
    u16 life;
    s32 x, y, vx, vy;
} Bullet;
#define MAX_BULLETS 12
static Bullet bullets[MAX_BULLETS];

void bullets_clear(void) { memset(bullets, 0, sizeof(bullets)); }

void bullet_fire(s32 x, s32 y, s32 vx, s32 vy) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) continue;
        Bullet* b = &bullets[i];
        b->active = 1;
        b->life = 240;
        b->x = x;
        b->y = y;
        /* HARD は敵弾が 1.25 倍速 */
        b->vx = game.hard ? vx + vx / 4 : vx;
        b->vy = game.hard ? vy + vy / 4 : vy;
        b->kind = 0;
        return;
    }
}

void bomb_drop(s32 x, s32 y, s32 vx) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) continue;
        Bullet* b = &bullets[i];
        b->active = 1;
        b->life = 200;
        b->x = x;
        b->y = y;
        b->vx = vx;
        b->vy = 0x40;
        b->kind = 2;
        return;
    }
}

/* 爆弾の着弾: 爆風 (半径 20px) に入っていたらダメージ */
static void bomb_explode(int x, int y) {
    fx_spawn(FX_BOOM, x - 8, y - 12, 0);
    fx_spawn(FX_DUST, x - 12, y - 6, 0);
    fx_spawn(FX_DUST, x + 4, y - 6, 1);
    sfx_play(SFX_BOOM);
    shake_t = 4;
    shake_amp = 1;
    int pcx = FROM_FP(player.b.x) + player.b.w / 2, pcy = FROM_FP(player.b.y) + player.b.h / 2;
    if (ABS(pcx - x) < 20 && ABS(pcy - (y - 8)) < 22) player_damage(1, x);
}

void wave_fire(s32 x, s32 y, s32 vx) {
    bullet_fire(x, y, vx, 0);
    for (int i = MAX_BULLETS - 1; i >= 0; i--)
        if (bullets[i].active && bullets[i].x == x && bullets[i].y == y && bullets[i].vx == vx) {
            bullets[i].kind = 1;
            bullets[i].life = 150;
            break;
        }
}

void bullets_update(void) {
    int ax0, ay0, ax1, ay1, dmg, kbx, kby, stop;
    int atk = player_attack_box(&ax0, &ay0, &ax1, &ay1, &dmg, &kbx, &kby, &stop);
    int px = FROM_FP(player.b.x), py = FROM_FP(player.b.y);
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet* b = &bullets[i];
        if (!b->active) continue;
        if (b->kind == 2 && b->vy < 0x300) b->vy += 0x14; /* 爆弾は重力で加速 */
        b->x += b->vx;
        b->y += b->vy;
        int x = FROM_FP(b->x), y = FROM_FP(b->y);
        if (b->kind == 2) {
            int c = level_cell(x >> 4, (y + 3) >> 4);
            if (c == CELL_SOLID || c == CELL_ONEWAY || c == CELL_HAZARD) {
                bomb_explode(x, (y + 3) & ~15);
                b->active = 0;
                continue;
            }
        }
        if (--b->life == 0 || level_solid(x >> 4, y >> 4) || x < cam_x - 16 || x > cam_x + SCREEN_W + 16) {
            if (b->kind == 1 && b->life) fx_spawn(FX_DUST, x - 4, y, 0);
            if (b->life) fx_spawn(FX_SPARK, x - 4, y - 4, 0);
            b->active = 0;
            continue;
        }
        /* 斬撃波でも弾を消せる */
        {
            int hitw = 0;
            for (int k = 0; k < MAX_PWAVE; k++) {
                int wx0, wy0, wx1, wy1;
                u8 wid;
                if (b->kind != 1 && player_wave_box(k, &wx0, &wy0, &wx1, &wy1, &wid) &&
                    overlap(wx0, wy0, wx1, wy1, x - 3, y - 3, x + 3, y + 3))
                    hitw = 1;
            }
            if (hitw) {
                fx_spawn(FX_SPARK, x - 4, y - 4, 0);
                b->active = 0;
                continue;
            }
        }
        /* 斬撃で弾を消せる (衝撃波は不可) */
        if (b->kind != 1 && atk && overlap(ax0, ay0, ax1, ay1, x - 3, y - 3, x + 3, y + 3)) {
            fx_spawn(FX_SPARK, x - 4, y - 4, 0);
            sfx_play(SFX_HIT);
            game_add_chain(1);
            game_add_score(20);
            b->active = 0;
            continue;
        }
        int r = b->kind == 1 ? 6 : 2;
        int top = b->kind == 1 ? y - 2 : y - 2;
        if (overlap(px + 1, py + 2, px + player.b.w - 1, py + player.b.h, x - r, top, x + r, y + (b->kind == 1 ? 8 : 2))) {
            player_damage(1, x);
            b->active = 0;
        }
    }
}

void bullets_draw(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet* b = &bullets[i];
        if (!b->active) continue;
        if (b->kind == 2)
            oam_put(SPR_BOMB, FROM_FP(b->x) - 4 - cam_x, FROM_FP(b->y) - 4 - cam_y, 0, OBJPAL_NORMAL, 1);
        else if (b->kind == 1)
            oam_put(SPR_WAVE0 + ((b->life >> 2) & 1), FROM_FP(b->x) - 8 - cam_x, FROM_FP(b->y) - 8 - cam_y, b->vx > 0, OBJPAL_NORMAL, 1);
        else
            oam_put(SPR_BULLET, FROM_FP(b->x) - 4 - cam_x, FROM_FP(b->y) - 4 - cam_y, 0, OBJPAL_NORMAL, 1);
    }
}
