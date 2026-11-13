/* xprove.c -- the max mode's four new lanes on every input of their domains against codec/shzx_ref.h, and the fast
 * mode's lanes on the wider ranges the max mode feeds them (the mixer's sum: 23 inputs, |sum >> 16| <= 188,324).
 *   clang -std=c11 -O2 -fwrapv law/xprove.c -o law/xprove && law/xprove */
#include <stdio.h>
#include <stdint.h>
#define SHZ_LANES_ONLY
#include "../codec/shz_laws.h"
#include "../codec/shzx_laws.h"
#undef SHZ_NLIM
#undef SHZ_LRS
#undef SHZ_CAP
#include "../codec/shz_ref.h"
#include "../codec/shzx_ref.h"
#define OPAQUE(v) __asm__ volatile("" : "+r"(v))
static long bad;
static void report(const char *name, const char *dom, long n, long b) { printf("  %-9s %-46s %12ld inputs  %ld wrong\n", name, dom, n, b); bad += b; }
int main(void) {
    long b1 = 0, n = 0;
    for (uint64_t i = 0; i < (1ull << 32); i++) { uint32_t x = (uint32_t)i; OPAQUE(x); b1 += (uint32_t)lw_XHM1((int32_t)x) != L_HM1(x); }
    report("XHM1", "all 2^32 words", 1l << 32, b1);
    long bi = 0, bd = 0; n = 0;
    for (uint32_t R = 1; R <= 7; R++) for (uint32_t p = 0; p < 65536; p++) {
        uint32_t x = p | R << 16; OPAQUE(x);
        bi += (uint32_t)lw_XINC16((int32_t)x) != L_INC16(x); bd += (uint32_t)lw_XDEC16((int32_t)x) != L_DEC16(x); n++;
    }
    report("XINC16", "all p in [0, 65535], R in [1, 7]", n, bi);
    report("XDEC16", "all p in [0, 65535], R in [1, 7]", n, bd);
    b1 = 0; n = 0;
    for (int32_t d = -4095; d <= 4095; d++) for (int32_t w = 0; w < 128; w++) {
        uint32_t y = (uint32_t)(d + 4096) | (uint32_t)w << 13; OPAQUE(y);
        b1 += (uint32_t)lw_XSDELTA((int32_t)y) != L_SDELTA(y); n++;
    }
    report("XSDELTA", "all d in [-4095, 4095], w in [0, 127]", n, b1);
    b1 = 0; n = 0;
    for (int32_t v = -(1 << 18); v <= (1 << 18); v++) { b1 += lw_CLHI(lw_CLLO(v)) != (v < -2047 ? -2047 : v > 2047 ? 2047 : v); n++; }
    report("CLAMP12", "join CLHI(CLLO(v)), all of [-2^18, 2^18]", n, b1);
    printf("%s: %ld wrong in all\n", bad ? "FAIL" : "PASS", bad);
    return bad != 0;
}
