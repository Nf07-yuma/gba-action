/* 標準関数の最小実装・乱数・入力 */
#include "game.h"

void (*irq_user_hook)(u32 flags) = 0;

void* memcpy(void* dst, const void* src, unsigned n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    if ((((u32)d | (u32)s | n) & 3) == 0) {
        u32* dw = (u32*)d;
        const u32* sw = (const u32*)s;
        for (n >>= 2; n; n--) *dw++ = *sw++;
        return dst;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void* memset(void* dst, int c, unsigned n) {
    u8* d = (u8*)dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

static u32 rng_state = 0x1234567u;
u32 rng_next(void) {
    u32 x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}
int rng_range(int n) { return n > 0 ? (int)((rng_next() >> 8) % (u32)n) : 0; }

u16 key_cur, key_prev;
void input_update(void) {
    key_prev = key_cur;
    key_cur = ~REG_KEYINPUT & KEY_MASK;
}
