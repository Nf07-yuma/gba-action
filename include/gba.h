/* GBA ハードウェア定義 (最小限・自前実装) */
#ifndef GBA_H
#define GBA_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed char    s8;
typedef signed short   s16;
typedef signed int     s32;
typedef volatile u16   vu16;
typedef volatile u32   vu32;

#define IWRAM_CODE __attribute__((section(".iwram"), long_call, target("arm")))
#define EWRAM_BSS  __attribute__((section(".ewram_bss")))
#define ALIGN4     __attribute__((aligned(4)))
#define INLINE     static inline

/* ---- I/O レジスタ ---- */
#define REG_BASE      0x04000000
#define REG_DISPCNT   (*(vu16*)(REG_BASE + 0x0000))
#define REG_DISPSTAT  (*(vu16*)(REG_BASE + 0x0004))
#define REG_VCOUNT    (*(vu16*)(REG_BASE + 0x0006))
#define REG_BGCNT(n)  (*(vu16*)(REG_BASE + 0x0008 + (n) * 2))
#define REG_BGHOFS(n) (*(vu16*)(REG_BASE + 0x0010 + (n) * 4))
#define REG_BGVOFS(n) (*(vu16*)(REG_BASE + 0x0012 + (n) * 4))
#define REG_WIN0H     (*(vu16*)(REG_BASE + 0x0040))
#define REG_WIN0V     (*(vu16*)(REG_BASE + 0x0044))
#define REG_WININ     (*(vu16*)(REG_BASE + 0x0048))
#define REG_WINOUT    (*(vu16*)(REG_BASE + 0x004A))
#define REG_MOSAIC    (*(vu16*)(REG_BASE + 0x004C))
#define REG_BLDCNT    (*(vu16*)(REG_BASE + 0x0050))
#define REG_BLDALPHA  (*(vu16*)(REG_BASE + 0x0052))
#define REG_BLDY      (*(vu16*)(REG_BASE + 0x0054))
#define REG_KEYINPUT  (*(vu16*)(REG_BASE + 0x0130))
#define REG_IE        (*(vu16*)(REG_BASE + 0x0200))
#define REG_IF        (*(vu16*)(REG_BASE + 0x0202))
#define REG_WAITCNT   (*(vu16*)(REG_BASE + 0x0204))
#define REG_IME       (*(vu16*)(REG_BASE + 0x0208))

/* サウンド */
#define REG_SOUND1CNT_L (*(vu16*)(REG_BASE + 0x0060))
#define REG_SOUND1CNT_H (*(vu16*)(REG_BASE + 0x0062))
#define REG_SOUND1CNT_X (*(vu16*)(REG_BASE + 0x0064))
#define REG_SOUND2CNT_L (*(vu16*)(REG_BASE + 0x0068))
#define REG_SOUND2CNT_H (*(vu16*)(REG_BASE + 0x006C))
#define REG_SOUND3CNT_L (*(vu16*)(REG_BASE + 0x0070))
#define REG_SOUND3CNT_H (*(vu16*)(REG_BASE + 0x0072))
#define REG_SOUND3CNT_X (*(vu16*)(REG_BASE + 0x0074))
#define REG_SOUND4CNT_L (*(vu16*)(REG_BASE + 0x0078))
#define REG_SOUND4CNT_H (*(vu16*)(REG_BASE + 0x007C))
#define REG_SOUNDCNT_L  (*(vu16*)(REG_BASE + 0x0080))
#define REG_SOUNDCNT_H  (*(vu16*)(REG_BASE + 0x0082))
#define REG_SOUNDCNT_X  (*(vu16*)(REG_BASE + 0x0084))
#define REG_SOUNDBIAS   (*(vu16*)(REG_BASE + 0x0088))
#define WAVE_RAM        ((vu32*)(REG_BASE + 0x0090))

/* DMA */
#define REG_DMA3SAD   (*(vu32*)(REG_BASE + 0x00D4))
#define REG_DMA3DAD   (*(vu32*)(REG_BASE + 0x00D8))
#define REG_DMA3CNT   (*(vu32*)(REG_BASE + 0x00DC))
#define DMA_ENABLE    0x80000000
#define DMA_32        0x04000000
#define DMA_16        0x00000000
#define DMA_SRC_FIX   0x01000000

/* DISPCNT */
#define DCNT_MODE0    0x0000
#define DCNT_OBJ_1D   0x0040
#define DCNT_BLANK    0x0080
#define DCNT_BG0      0x0100
#define DCNT_BG1      0x0200
#define DCNT_BG2      0x0400
#define DCNT_BG3      0x0800
#define DCNT_OBJ      0x1000
#define DCNT_WIN0     0x2000

/* BGCNT */
#define BG_PRIO(n)    (n)
#define BG_CBB(n)     ((n) << 2)
#define BG_SBB(n)     ((n) << 8)
#define BG_4BPP       0x0000
#define BG_SIZE_32x32 0x0000
#define BG_SIZE_64x32 0x4000
#define BG_SIZE_32x64 0x8000
#define BG_SIZE_64x64 0xC000

/* キー */
#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_SELECT 0x0004
#define KEY_START  0x0008
#define KEY_RIGHT  0x0010
#define KEY_LEFT   0x0020
#define KEY_UP     0x0040
#define KEY_DOWN   0x0080
#define KEY_R      0x0100
#define KEY_L      0x0200
#define KEY_MASK   0x03FF

/* 割り込み */
#define IRQ_VBLANK 0x0001
#define IRQ_TIMER0 0x0008
#define IRQ_TIMER1 0x0010

/* ---- メモリ ---- */
#define MEM_PAL      ((u16*)0x05000000)
#define PAL_BG       ((u16*)0x05000000)
#define PAL_OBJ      ((u16*)0x05000200)
#define MEM_VRAM     ((u16*)0x06000000)
#define CHARBLOCK(n) ((u16*)(0x06000000 + (n) * 0x4000))
#define SCREENBLOCK(n) ((u16*)(0x06000000 + (n) * 0x800))
#define OBJ_TILES    ((u16*)0x06010000)
#define MEM_OAM      ((u16*)0x07000000)

#define RGB15(r, g, b) ((u16)((r) | ((g) << 5) | ((b) << 10)))

/* OAM 属性 */
typedef struct {
    u16 attr0, attr1, attr2, fill;
} OamEntry;

#define ATTR0_Y(y)       ((y) & 0xFF)
#define ATTR0_HIDE       0x0200
#define ATTR0_BLEND      0x0400
#define ATTR0_SQUARE     0x0000
#define ATTR0_WIDE       0x4000
#define ATTR0_TALL       0x8000
#define ATTR1_X(x)       ((x) & 0x1FF)
#define ATTR1_HFLIP      0x1000
#define ATTR1_VFLIP      0x2000
#define ATTR1_SIZE(n)    ((n) << 14)
#define ATTR2_TILE(n)    ((n) & 0x3FF)
#define ATTR2_PRIO(n)    ((n) << 10)
#define ATTR2_PAL(n)     ((n) << 12)

/* BIOS */
INLINE void bios_vblank_wait(void) { __asm__ volatile("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory"); }

INLINE void dma3_copy32(void* dst, const void* src, u32 words) {
    REG_DMA3SAD = (u32)src;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = DMA_ENABLE | DMA_32 | words;
}
INLINE void dma3_fill32(void* dst, u32 value, u32 words) {
    static volatile u32 v;
    v = value;
    REG_DMA3SAD = (u32)&v;
    REG_DMA3DAD = (u32)dst;
    REG_DMA3CNT = DMA_ENABLE | DMA_32 | DMA_SRC_FIX | words;
}

#endif
