/* 主人公 STEEL: 移動・ジャンプ・ダッシュ・3段コンボ */
#include "game.h"

Player player;

#define PW 10
#define PH 24
#define GRAV_HOLD 0x38
#define GRAV      0x70
#define MAX_FALL  0x500
#define JUMP_V    0x4C0
#define RUN_MAX   0x1A0
#define ACC_GROUND 0x38
#define ACC_AIR   0x28
#define FRICTION  0x40
#define DASH_V    0x3C0
#define DASH_LEN  12
#define COYOTE    5
#define JUMP_BUF  6

typedef struct {
    u8 len, act0, act1, chain_at;
    s8 bx, by;
    u8 bw, bh;
    u8 dmg;
    s16 kbx, kby;
    u8 stop;
    u8 spr, fx;
    s8 fxx, fxy;
    s16 lunge;
    u8 sfx;
} AtkDef;

/* 0-2: 地上コンボ, 3: 空中攻撃 */
static const AtkDef atk_defs[4] = {
    /* len act0 act1 chain  bx   by  bw  bh dmg  kbx     kby   stop spr                fx              fxx fxy lunge  sfx */
    { 17, 2, 6, 6,    -2, -8, 32, 32, 1, 0x180, -0x100, 3, SPR_PLAYER_ATK1, SPR_SLASH1_0, 2, -6, 0x160, SFX_SLASH },
    { 17, 2, 6, 6,    -2, -20, 30, 44, 1, 0x0C0, -0x3C0, 3, SPR_PLAYER_ATK2, SPR_SLASH2_0, 2, -12, 0x120, SFX_SLASH },
    { 26, 4, 9, 0,    -2, -14, 36, 44, 3, 0x440, -0x2C0, 7, SPR_PLAYER_ATK3, SPR_SLASH3_0, 4, -6, 0x2C0, SFX_SLASH3 },
    { 20, 2, 10, 0,  -21, -10, 42, 42, 1, 0x200, -0x200, 3, SPR_PLAYER_AIRATK, SPR_SLASHAIR_0, -11, -10, 0, SFX_SLASH },
};

static s32 ghost_x[3], ghost_y[3];

void player_init(s32 x, s32 y) {
    memset(&player, 0, sizeof(player));
    player.b.w = PW;
    player.b.h = PH;
    player.b.x = TO_FP(x);
    player.b.y = TO_FP(y);
    player.b.oneway = 1;
    player.dir = 1;
    player.hp_max = 8;
    player.hp = player.hp_max;
    player.state = PS_NORMAL;
    player.inv = 30;
}

static int center_x(void) { return FROM_FP(player.b.x) + PW / 2; }

static void start_attack(int step) {
    const AtkDef* a = &atk_defs[step];
    player.state = PS_ATTACK;
    player.combo = (u8)step;
    player.atk_t = 0;
    player.queued = 0;
    player.attack_id++;
    player.hit_any = 0;
    if (player.attack_id == 0) player.attack_id = 1;
    if (step < 3) {
        player.b.vx = player.dir * a->lunge;
    } else {
        if (player.air_attacks < 2 && player.b.vy > -0x100) player.b.vy = -0x180;
        player.air_attacks++;
    }
    sfx_play(a->sfx);
}

static void do_jump(void) {
    player.b.vy = -JUMP_V;
    player.coyote = 0;
    player.jump_buf = 0;
    player.state = PS_NORMAL;
    sfx_play(SFX_JUMP);
    fx_spawn(FX_DUST, center_x() - 4, FROM_FP(player.b.y) + PH - 6, 0);
}

static void start_dash(void) {
    player.state = PS_DASH;
    player.dash_t = DASH_LEN;
    player.dash_cd = 24;
    player.b.vx = player.dir * DASH_V;
    player.b.vy = 0;
    if (player.inv < DASH_LEN) player.inv = DASH_LEN;
    if (!player.b.on_ground) player.air_dash = 1;
    for (int i = 0; i < 3; i++) {
        ghost_x[i] = player.b.x;
        ghost_y[i] = player.b.y;
    }
    sfx_play(SFX_DASH);
    fx_spawn(FX_DUST, center_x() - 4 - player.dir * 6, FROM_FP(player.b.y) + PH - 6, 0);
}

static void apply_horizontal_input(int acc, int max) {
    int want = 0;
    if (key_down(KEY_LEFT)) want = -1;
    if (key_down(KEY_RIGHT)) want = 1;
    if (want) {
        player.dir = (s8)want;
        player.b.vx += want * acc;
        if (player.b.vx > max) player.b.vx = MAX(max, player.b.vx - FRICTION);
        if (player.b.vx < -max) player.b.vx = MIN(-max, player.b.vx + FRICTION);
    } else {
        if (player.b.vx > 0) player.b.vx = MAX(0, player.b.vx - FRICTION);
        else if (player.b.vx < 0) player.b.vx = MIN(0, player.b.vx + FRICTION);
    }
}

static void apply_gravity(void) {
    int g = (player.b.vy < 0 && key_down(KEY_A)) ? GRAV_HOLD : GRAV;
    player.b.vy += g;
    if (player.b.vy > MAX_FALL) player.b.vy = MAX_FALL;
}

void player_kill(void) {
    if (player.state == PS_DEAD) return;
    player.state = PS_DEAD;
    player.state_t = 0;
    player.hp = 0;
    int cx = center_x(), cy = FROM_FP(player.b.y) + PH / 2;
    fx_spawn(FX_BOOM, cx - 8, cy - 8, 0);
    fx_spawn(FX_BOOM, cx - 14, cy - 2, 0);
    fx_spawn(FX_BOOM, cx - 2, cy - 14, 0);
    sfx_play(SFX_BOOM);
    shake_t = 20;
    shake_amp = 3;
    game_on_player_dead();
}

void player_damage(int dmg, int from_x) {
    if (player.inv || player.state == PS_DEAD || player.state == PS_CLEAR) return;
    player.hp -= dmg;
    game.hud_dirty = 1;
    if (player.hp <= 0) {
        player_kill();
        return;
    }
    player.state = PS_HURT;
    player.state_t = 0;
    player.b.vx = (center_x() < from_x ? -1 : 1) * 0x200;
    player.b.vy = -0x300;
    player.inv = 90;
    shake_t = 8;
    shake_amp = 2;
    game.hitstop = 4;
    sfx_play(SFX_HURT);
}

int player_attack_box(int* x0, int* y0, int* x1, int* y1, int* dmg, int* kbx, int* kby, int* stop) {
    if (player.state != PS_ATTACK) return 0;
    const AtkDef* a = &atk_defs[player.combo];
    if (player.atk_t < a->act0 || player.atk_t > a->act1) return 0;
    int cx = center_x();
    int top = FROM_FP(player.b.y);
    int bx = (player.dir > 0) ? cx + a->bx : cx - a->bx - a->bw;
    *x0 = bx;
    *x1 = bx + a->bw;
    *y0 = top + a->by;
    *y1 = top + a->by + a->bh;
    *dmg = a->dmg;
    *kbx = a->kbx;
    *kby = a->kby;
    *stop = a->stop;
    return 1;
}

void player_update(void) {
    Player* p = &player;
    p->state_t++;
    p->anim_t++;
    if (p->inv) p->inv--;
    if (p->dash_cd) p->dash_cd--;
    if (p->state == PS_DEAD || p->state == PS_CLEAR) {
        if (p->state == PS_CLEAR) {
            /* ゲートに吸い込まれる演出: 徐々に上昇 */
            p->b.vx = 0;
            p->b.vy = -0x80;
            p->b.y += p->b.vy;
        }
        return;
    }

    /* ジャンプ入力バッファ / コヨーテタイム */
    if (key_hit(KEY_A)) p->jump_buf = JUMP_BUF;
    else if (p->jump_buf) p->jump_buf--;
    if (p->b.on_ground) {
        p->coyote = COYOTE;
        p->air_dash = 0;
        p->air_attacks = 0;
    } else if (p->coyote) {
        p->coyote--;
    }

    switch (p->state) {
    case PS_NORMAL:
        apply_horizontal_input(p->b.on_ground ? ACC_GROUND : ACC_AIR, RUN_MAX);
        /* 足場すり抜け: 下 + A */
        if (p->jump_buf && key_down(KEY_DOWN) && p->b.on_ground) {
            int bot = FROM_FP(p->b.y) + PH;
            int l = FROM_FP(p->b.x) >> 4, r = (FROM_FP(p->b.x) + PW - 1) >> 4;
            if (level_cell(l, bot >> 4) == CELL_ONEWAY || level_cell(r, bot >> 4) == CELL_ONEWAY) {
                p->b.drop = 1;
                p->jump_buf = 0;
                p->b.y += TO_FP(1);
            }
        }
        if (p->jump_buf && p->coyote) do_jump();
        if (key_released(KEY_A) && p->b.vy < -0x200) p->b.vy = -0x200;
        if (key_hit(KEY_B)) {
            start_attack(p->b.on_ground ? 0 : 3);
        } else if (key_hit(KEY_R | KEY_L) && !p->dash_cd && (p->b.on_ground || !p->air_dash)) {
            start_dash();
        }
        apply_gravity();
        break;

    case PS_ATTACK: {
        const AtkDef* a = &atk_defs[p->combo];
        p->atk_t++;
        if (key_hit(KEY_B)) p->queued = 1;
        /* 摩擦で踏み込みを減速 */
        if (p->combo < 3) {
            if (p->b.vx > 0) p->b.vx = MAX(0, p->b.vx - 0x28);
            else if (p->b.vx < 0) p->b.vx = MIN(0, p->b.vx + 0x28);
            /* 攻撃中でも向きは入れ替え可能(発生前のみ) */
            if (p->atk_t < a->act0) {
                if (key_down(KEY_LEFT)) p->dir = -1;
                if (key_down(KEY_RIGHT)) p->dir = 1;
            }
        } else {
            apply_horizontal_input(ACC_AIR, RUN_MAX);
        }
        apply_gravity();
        if (p->combo < 3 && a->chain_at && p->atk_t >= a->chain_at && p->queued) {
            if (key_down(KEY_LEFT)) p->dir = -1;
            if (key_down(KEY_RIGHT)) p->dir = 1;
            start_attack(p->combo + 1);
            break;
        }
        /* 硬直のキャンセル: ジャンプ / ダッシュ */
        if (p->atk_t > a->act1 + 2) {
            if (p->jump_buf && p->coyote) {
                do_jump();
                break;
            }
            if (key_hit(KEY_R | KEY_L) && !p->dash_cd && (p->b.on_ground || !p->air_dash)) {
                start_dash();
                break;
            }
        }
        if (p->combo == 3 && p->b.on_ground && p->atk_t > 3) {
            p->state = PS_NORMAL;
            break;
        }
        if (p->atk_t >= a->len) {
            p->state = PS_NORMAL;
            if (p->combo == 3 && key_hit(KEY_B)) start_attack(3);
        }
        break;
    }

    case PS_DASH:
        p->b.vy = 0;
        p->b.vx = p->dir * DASH_V;
        if (--p->dash_t == 0) {
            p->state = PS_NORMAL;
            p->b.vx = p->dir * RUN_MAX;
        }
        if (key_hit(KEY_B)) {
            p->b.vx = p->dir * RUN_MAX;
            start_attack(p->b.on_ground ? 0 : 3);
        } else if (p->jump_buf && p->coyote) {
            do_jump();
            p->b.vx = p->dir * (RUN_MAX + 0x60);
        }
        break;

    case PS_HURT:
        apply_gravity();
        if (p->b.vx > 0) p->b.vx = MAX(0, p->b.vx - 0x10);
        else if (p->b.vx < 0) p->b.vx = MIN(0, p->b.vx + 0x10);
        if (p->state_t > 20) p->state = PS_NORMAL;
        break;
    }

    int was_air = !p->b.on_ground;
    s32 vy_before = p->b.vy;
    body_move(&p->b);
    if (was_air && p->b.on_ground && vy_before > 0x300) {
        fx_spawn(FX_DUST, center_x() - 10, FROM_FP(p->b.y) + PH - 6, 0);
        fx_spawn(FX_DUST, center_x() + 2, FROM_FP(p->b.y) + PH - 6, 1);
    }

    /* 残像の位置 */
    if (p->state == PS_DASH && (p->anim_t & 1)) {
        ghost_x[2] = ghost_x[1];
        ghost_y[2] = ghost_y[1];
        ghost_x[1] = ghost_x[0];
        ghost_y[1] = ghost_y[0];
        ghost_x[0] = p->b.x;
        ghost_y[0] = p->b.y;
    }

    /* 落下死 / トゲ */
    if (FROM_FP(p->b.y) > level.px_h + 16) {
        player_kill();
        return;
    }
    if (body_touch_hazard(&p->b)) {
        if (!p->inv) {
            player_damage(2, center_x() + p->dir * 8);
            p->b.vy = -0x480;
        }
    }
}

void player_draw(void) {
    Player* p = &player;
    if (p->state == PS_DEAD) return;
    int sx = FROM_FP(p->b.x) + PW / 2 - 16 - cam_x;
    int sy = FROM_FP(p->b.y) + PH - 31 - cam_y;
    int flip = p->dir < 0;
    int spr, pal = OBJPAL_NORMAL;

    switch (p->state) {
    case PS_HURT:
        spr = SPR_PLAYER_HURT;
        if (p->state_t & 2) pal = OBJPAL_HURT;
        break;
    case PS_DASH:
        spr = SPR_PLAYER_DASH;
        break;
    case PS_ATTACK: {
        const AtkDef* a = &atk_defs[p->combo];
        spr = a->spr;
        /* 斬撃エフェクト */
        if (p->atk_t >= a->act0 && p->atk_t <= a->act1 + 4) {
            int fspr = a->fx + (p->atk_t > a->act1 ? 1 : 0);
            int fx = flip ? sx + 32 - 32 - a->fxx - 16 : sx + 16 + a->fxx;
            if (p->combo == 3) fx = sx + 16 + a->fxx - (flip ? 10 : 0);
            oam_put(fspr, fx, sy + a->fxy + 6, flip, OBJPAL_NORMAL, 1);
        }
        break;
    }
    case PS_CLEAR:
        spr = SPR_PLAYER_JUMP;
        if (p->state_t & 1) pal = OBJPAL_FLASH;
        break;
    default:
        if (p->b.on_ground) {
            if (ABS(p->b.vx) > 0x40) {
                spr = SPR_PLAYER_RUN0 + (p->anim_t / 5) % 6;
            } else {
                spr = SPR_PLAYER_IDLE0 + ((p->anim_t / 32) & 1);
            }
        } else {
            spr = p->b.vy < 0 ? SPR_PLAYER_JUMP : SPR_PLAYER_FALL;
        }
        break;
    }
    if (p->inv && p->state != PS_HURT && p->state != PS_DASH && (p->inv & 2)) return;
    oam_put(spr, sx, sy, flip, pal, 1);
    if (p->state == PS_DASH) {
        for (int i = 0; i < 3; i++) {
            if ((p->anim_t + i) & 1) continue;
            oam_put(SPR_PLAYER_DASH, FROM_FP(ghost_x[i]) + PW / 2 - 16 - cam_x, FROM_FP(ghost_y[i]) + PH - 31 - cam_y,
                    flip, OBJPAL_GHOST, 2);
        }
    }
}
