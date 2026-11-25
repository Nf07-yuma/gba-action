/* ハイスコア保存 (SRAM 32KB, 8bit アクセスのみ) */
#include "game.h"

#define SRAM ((volatile u8*)0x0E000000)
#define SAVE_MAGIC 0x31435453u /* 'STC1' */

/* エミュレータ/フラッシュカートにセーブ種別を知らせる識別文字列 */
__attribute__((used, aligned(4))) const char save_type_id[] = "SRAM_V113";

u32 save_hiscore;
u16 save_best_chain;
u8 save_cleared;
u8 save_reached; /* 到達した最も先のステージ (0 始まり) */

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
    save_reached = 0;
    if (rd32(0) != SAVE_MAGIC) return;
    u32 hs = rd32(4), ch = rd32(8), cl = rd32(12), sum = rd32(16);
    if ((hs ^ ch ^ cl ^ 0x5A5A5A5A) != sum) return;
    save_hiscore = hs;
    save_best_chain = (u16)ch;
    save_cleared = (u8)(cl & 1);
    save_reached = (u8)MIN(4u, (cl >> 8) & 0xFF);
}

static void save_write(void) {
    u32 ch = save_best_chain, cl = save_cleared | ((u32)save_reached << 8);
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

/* ステージ到達を記録 (ステージセレクト用) */
void save_reach_stage(int stage) {
    if (stage > save_reached) {
        save_reached = (u8)stage;
        save_write();
    }
}
