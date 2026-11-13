/* aprove.c -- ORD32 and UNORD32 on all 2^32 words against codec/shza_ref.h, and that each undoes the other. */
#include <stdio.h>
#include <stdint.h>
#define SHZ_LANES_ONLY
#include "../codec/shza_laws.h"
#include "../codec/shza_ref.h"
#define OPAQUE(v) __asm__ volatile("" : "+r"(v))
int main(void) {
    long b1 = 0, b2 = 0, b3 = 0;
    for (uint64_t i = 0; i < (1ull << 32); i++) {
        uint32_t x = (uint32_t)i; OPAQUE(x);
        uint32_t o = (uint32_t)lw_AORD32((int32_t)x), u = (uint32_t)lw_AUNORD32((int32_t)x);
        b1 += o != L_ORD32(x); b2 += u != L_UNORD32(x); b3 += (uint32_t)lw_AUNORD32((int32_t)o) != x;
    }
    printf("  AORD32    all 2^32 words  %ld wrong\n  AUNORD32  all 2^32 words  %ld wrong\n  UNORD32(ORD32(x)) == x on all 2^32 words: %ld wrong\n", b1, b2, b3);
    printf("%s\n", b1 + b2 + b3 ? "FAIL" : "PASS: 0 wrong in all");
    return (b1 + b2 + b3) != 0;
}
