/* ハイスコア保存 (SRAM 32KB, 8bit アクセスのみ) */
#include "game.h"

#define SRAM ((volatile u8*)0x0E000000)
#define SAVE_MAGIC 0x31435453u /* 'STC1' */

/* エミュレータ/フラッシュカートにセーブ種別を知らせる識別文字列 */
__attribute__((used, aligned(4))) const char save_type_id[] = "SRAM_V113";

u32 save_hiscore;
u16 save_best_chain;
u8 save_cleared;
u8 save_clear_diff; /* クリアした難易度 bit0=EASY bit1=NORMAL bit2=HARD */
u8 save_reached; /* 到達した最も先のステージ (0 始まり) */
u8 save_rank[5];  /* ステージ別の最高ランク+1 (0=未記録, 1=C .. 4=S) */
u16 save_best_time[5]; /* ステージ別のベストタイム (フレーム, 0=未記録) */

/* ベストタイムは別ブロック (オフセット 32〜): magic 'STT1' + u16 x 5 + チェックサム */
#define TIME_OFF 32
#define TIME_MAGIC 0x31545453u

static u32 rd32(int off) {
    return SRAM[off] | (SRAM[off + 1] << 8) | (SRAM[off + 2] << 16) | ((u32)SRAM[off + 3] << 24);
}
static void wr32(int off, u32 v) {
    for (int i = 0; i < 4; i++) SRAM[off + i] = (u8)(v >> (i * 8));
}

void save_load(void) {
    save_hiscore = 0;
    save_best_chain = 0;
    save_cleared = 0;
    save_clear_diff = 0;
    save_reached = 0;
    for (int i = 0; i < 5; i++) save_rank[i] = 0;
    for (int i = 0; i < 5; i++) save_best_time[i] = 0;
    if (rd32(TIME_OFF) == TIME_MAGIC) {
        u32 sum = 0x5A5A5A5A;
        u16 tm[5];
        for (int i = 0; i < 5; i++) {
            tm[i] = (u16)(SRAM[TIME_OFF + 4 + i * 2] | (SRAM[TIME_OFF + 5 + i * 2] << 8));
            sum ^= (u32)tm[i] << ((i & 1) * 16);
        }
        if (sum == rd32(TIME_OFF + 14))
            for (int i = 0; i < 5; i++) save_best_time[i] = tm[i];
    }
    if (rd32(0) != SAVE_MAGIC) return;
    u32 hs = rd32(4), ch = rd32(8), cl = rd32(12), sum = rd32(16);
    if ((hs ^ ch ^ cl ^ 0x5A5A5A5A) != sum) return;
    save_hiscore = hs;
    save_best_chain = (u16)ch;
    save_cleared = (u8)(cl & 1);
    /* bit1: BGM オフ, bit2-3: 難易度 (0=NORMAL 1=EASY 2=HARD) */
    bgm_mute = (u8)((cl >> 1) & 1);
    game.easy = (u8)(((cl >> 2) & 3) == 1);
    game.hard = (u8)(((cl >> 2) & 3) == 2);
    save_clear_diff = (u8)((cl >> 4) & 7); /* bit4-6 */
    save_reached = (u8)MIN(4u, (cl >> 8) & 0xFF);
    for (int i = 0; i < 5; i++) save_rank[i] = (u8)MIN(4u, (cl >> (16 + i * 3)) & 7);
}

static void save_write(void) {
    u32 ch = save_best_chain, cl = save_cleared | ((u32)save_reached << 8);
    cl |= (u32)(bgm_mute & 1) << 1;
    cl |= (u32)(game.easy ? 1 : game.hard ? 2 : 0) << 2;
    cl |= (u32)(save_clear_diff & 7) << 4;
    /* bit16- : ステージ別ランク (3bit x 5) */
    for (int i = 0; i < 5; i++) cl |= (u32)save_rank[i] << (16 + i * 3);
    wr32(4, save_hiscore);
    wr32(8, ch);
    wr32(12, cl);
    wr32(16, save_hiscore ^ ch ^ cl ^ 0x5A5A5A5A);
    wr32(0, SAVE_MAGIC);
}

/* 記録更新があれば書き込み、更新したら 1 を返す */
int save_record(u32 score, u16 chain, int cleared) {
    int upd = 0;
    if (score > save_hiscore) {
        save_hiscore = score;
        upd = 1;
    }
    if (chain > save_best_chain) {
        save_best_chain = chain;
        upd = 1;
    }
    if (cleared && !save_cleared) {
        save_cleared = 1;
        upd = 1;
    }
    if (upd) save_write();
    return upd;
}

/* クリアした難易度を記録 (0=EASY 1=NORMAL 2=HARD)。初めてなら 1 */
int save_clear_difficulty(int d) {
    u8 bit = (u8)(1 << d);
    if (save_clear_diff & bit) return 0;
    save_clear_diff |= bit;
    save_write();
    return 1;
}

/* 設定 (BGM オン/オフ・難易度) を保存 */
void save_settings(void) {
    save_write();
}

/* ステージ到達を記録 (ステージセレクト用) */
void save_reach_stage(int stage) {
    if (stage > save_reached) {
        save_reached = (u8)stage;
        save_write();
    }
}

/* ステージのランク (0=C..3=S) を記録。自己ベスト更新なら 1 を返す */
int save_rank_stage(int stage, int rank) {
    if (stage < 0 || stage >= 5 || rank + 1 <= save_rank[stage]) return 0;
    save_rank[stage] = (u8)(rank + 1);
    save_write();
    return 1;
}

/* ステージのクリアタイムを記録。ベスト更新なら 1 を返す */
int save_time_stage(int stage, u32 frames) {
    if (stage < 0 || stage >= 5) return 0;
    u16 f = (u16)MIN(frames, 65535u);
    if (save_best_time[stage] && f >= save_best_time[stage]) return 0;
    save_best_time[stage] = f;
    u32 sum = 0x5A5A5A5A;
    for (int i = 0; i < 5; i++) {
        SRAM[TIME_OFF + 4 + i * 2] = (u8)save_best_time[i];
        SRAM[TIME_OFF + 5 + i * 2] = (u8)(save_best_time[i] >> 8);
        sum ^= (u32)save_best_time[i] << ((i & 1) * 16);
    }
    wr32(TIME_OFF + 14, sum);
    wr32(TIME_OFF, TIME_MAGIC);
    return 1;
}
