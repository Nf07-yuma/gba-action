/* STEEL CHAIN 共通定義 */
#ifndef GAME_H
#define GAME_H

#include "gba.h"
#include "assets.h"

#define FP 8
#define TO_FP(x)   ((s32)(x) * (1 << FP))
#define FROM_FP(x) ((s32)(x) >> FP)
#define CELL 16
#define SCREEN_W 240
#define SCREEN_H 160

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* ---- util.c ---- */
void* memcpy(void* dst, const void* src, unsigned n);
void* memset(void* dst, int c, unsigned n);
u32 rng_next(void);
int rng_range(int n);
extern void (*irq_user_hook)(u32 flags);

/* ---- 入力 ---- */
extern u16 key_cur, key_prev;
void input_update(void);
INLINE int key_down(u16 k) { return key_cur & k; }
INLINE int key_hit(u16 k) { return (key_cur & ~key_prev) & k; }
INLINE int key_released(u16 k) { return (~key_cur & key_prev) & k; }

/* ---- video.c ---- */
#define BG_HUD   0
#define BG_LEVEL 1
#define BG_SKY   2
#define SBB_LEVEL 28  /* 28,29: 64x32 */
#define SBB_HUD   30
#define SBB_SKY   31
#define CBB_LEVEL 0
#define CBB_UI    1
#define CBB_SKY   2

extern u16 sprite_tile_base[SPR_COUNT];
void video_init(void);
void oam_begin(void);
void oam_end(void);
/* 画面座標に描画。flip: 左右反転, pal: OBJPAL_*, prio: 0-3 */
void oam_put(int spr, int x, int y, int flip, int pal, int prio);
void oam_put_blend(int spr, int x, int y, int flip, int pal, int prio);
void sprite_request_dynamic(int spr);
void sprite_flush_dynamic(void);
void vsync(void);

/* ---- テキスト (BG0) ---- */
void text_clear(void);
void text_put(int x, int y, const char* s, int pal);
void text_put_center(int y, const char* s, int pal);
void text_num(int x, int y, u32 n, int digits, int pal);
void text_fill(int x, int y, int w, int h, int tile, int pal);

/* ---- 物理ボディ ---- */
typedef struct {
    s32 x, y;     /* 左上 (固定小数) */
    s32 vx, vy;
    s16 w, h;     /* ピクセル */
    u8 on_ground;
    u8 hit_wall;
    u8 oneway;    /* 一方通行足場に乗るか */
    u8 drop;      /* 足場をすり抜け中 */
} Body;

/* ---- level.c ---- */
typedef struct {
    const LevelDef* def;
    int index;
    int w, h;        /* セル単位 */
    int px_w, px_h;  /* ピクセル */
} Level;
extern Level level;
extern s32 cam_x, cam_y;
extern int shake_t, shake_amp;

void level_load(int index);
int level_cell(int cx, int cy);
int level_solid(int cx, int cy);
void level_scroll_update(void);
void level_force_redraw(void);
void body_move(Body* b);
int body_touch_hazard(const Body* b);
void camera_update(s32 target_x, s32 target_y, int dir);
void camera_snap(s32 target_x, s32 target_y);

/* ---- player.c ---- */
enum { PS_NORMAL, PS_ATTACK, PS_DASH, PS_HURT, PS_DEAD, PS_CLEAR };
typedef struct {
    Body b;
    u8 state;
    s8 dir;           /* 1=右, -1=左 */
    s16 hp, hp_max;
    u16 inv;          /* 無敵時間 */
    u8 combo;         /* 0..2 地上コンボ段, 3=空中 */
    u8 atk_t;
    u8 queued;
    u8 attack_id;
    u8 coyote;
    u8 jump_buf;
    u8 dash_t;
    u8 dash_cd;
    u8 air_dash;
    u8 air_attacks;
    u16 state_t;
    u16 anim_t;
    u8 hit_any;       /* 今の攻撃で何か当てたか */
} Player;
extern Player player;
void player_init(s32 x, s32 y);
void player_update(void);
void player_draw(void);
void player_damage(int dmg, int from_x);
int player_attack_box(int* x0, int* y0, int* x1, int* y1, int* dmg, int* kbx, int* kby, int* stop);
void player_kill(void);

/* ---- enemy.c ---- */
#define MAX_ENEMIES 16
void enemies_reset(int full);
void enemies_spawn_check(void);
void enemies_update(void);
void enemies_draw(void);
int enemies_alive(void);
void lifts_apply(Body* b);
int lifts_debug(s32* xy, int max);

/* ---- objects (bullets / items / checkpoints) ---- */
void bullet_fire(s32 x, s32 y, s32 vx, s32 vy);
void bullets_clear(void);
void bullets_update(void);
void bullets_draw(void);
void item_spawn(int type, s32 x, s32 y);
void items_clear(void);
void items_update(void);
void items_draw(void);

/* ---- boss.c ---- */
extern s32 boss_lock_x;
void boss_reset(void);
int boss_check_start(int spawn_cx, int spawn_cy, int param);
/* core.c (最終ボス) */
int core_active(void);
int core_hp(void);
int core_hp_max(void);
int core_state_id(void);
int core_x(void);
int core_dead(void);
void core_reset(void);
void core_start(int spawn_cx, int spawn_cy);
void core_update(void);
void core_draw(void);
void boss_update(void);
void boss_draw(void);
void boss_hud(void);
int boss_active(void);
int boss_hp(void);
int boss_state_id(void);
int boss_x(void);
void wave_fire(s32 x, s32 y, s32 vx);

/* ---- fx.c ---- */
enum { FX_BOOM, FX_SPARK, FX_DUST, FX_SLASH, FX_DEBRIS };
void fx_debris(int x, int y, int n);
void screen_flash(int frames);
void fx_flash_apply(void);
void fx_clear(void);
void fx_spawn(int type, int x, int y, int flip);
void fx_spawn_slash(int spr0, int x, int y, int flip);
void fx_update(void);
void fx_draw(void);

/* ---- game.c ---- */
enum { GS_TITLE, GS_STAGE_INTRO, GS_PLAY, GS_DEAD, GS_CLEAR, GS_GAMEOVER, GS_ENDING, GS_PAUSE };
typedef struct {
    u8 state;
    u8 stage;
    s8 lives;
    u32 score;
    u16 chain;
    u16 chain_t;
    u16 chain_best;
    u16 hitstop;
    u32 frame;
    u32 state_t;
    s32 checkpoint_x, checkpoint_y;
    u16 kills;
    u16 deaths;
    u8 hud_dirty;
} Game;
extern Game game;
void game_add_chain(int hits);
void game_add_score(u32 pts);
void game_set_checkpoint(s32 x, s32 y);
void game_stage_clear(void);
void game_on_player_dead(void);
void game_boss_defeated(int x, int y);
void hud_draw(int force);

/* ---- save.c ---- */
extern u32 save_hiscore;
extern u16 save_best_chain;
extern u8 save_cleared;
void save_load(void);
int save_record(u32 score, u16 chain, int cleared);

/* ---- sound.c ---- */
enum { SFX_JUMP, SFX_SLASH, SFX_SLASH3, SFX_HIT, SFX_BOOM, SFX_HURT, SFX_PICKUP, SFX_DASH, SFX_SHOT, SFX_SELECT, SFX_CHECK };
void sound_init(void);
void sfx_play(int id);
void sound_update(void);
void music_play(int id);
void music_stop(void);
void music_pause(int on);

/* ---- テスト用デバッグブロック (EWRAM 先頭固定) ---- */
typedef struct {
    u32 magic;        /* 'STCH' */
    u32 frame;
    u32 state;
    u32 stage;
    s32 px, py;
    s32 hp;
    s32 lives;
    u32 score;
    u32 kills;
    u32 deaths;
    u32 chain_best;
    u32 enemies;
    u32 cam_x;
    u32 clears;
    u32 level_cells; /* ステージセル配列のアドレス (ボット用) */
    u32 level_w, level_h;
    s32 vy;
    u32 on_ground;
    s32 boss_hp;
    u32 boss_state;
    s32 boss_x;
    u32 cmd;      /* テストランナーからの命令: 1=ステージ移動(arg), 2=X座標へワープ(arg) */
    u32 cmd_arg;
    u32 lift_count;
    s32 lifts[8]; /* x,y x4 */
    u32 obj_tiles; /* 使用中の OBJ タイル数 (1024 以下であること) */
    u32 lag_frames;  /* 処理が 1 フレームに収まらなかった回数 */
    u32 max_lines;   /* 1 フレームの処理に使った最大走査線数 (228 で 1 フレーム) */
    u32 max_state, lag_state; /* 発生時の画面状態 | (経過フレーム << 8) */
} DebugInfo;
extern volatile DebugInfo g_debug;
#define DEBUG_MAGIC 0x48435453u

#endif
