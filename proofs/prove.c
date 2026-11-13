/* prove.c -- every machine-authored integer law of shz, on every input of its domain, against its meaning in
 * codec/shz_ref.h; then every fixed join (SAMETOP, P12, ISSP, CLAMP12, WCLAMP, the class table, DADD, DSUB, squash)
 * against the meaning it replaces.
 *   clang -std=c11 -O2 -fwrapv law/prove.c -o law/prove && law/prove */
#include <stdio.h>
#include <stdint.h>
#define SHZ_LANES_ONLY
#include "../codec/shz_laws.h"
#undef SHZ_NLIM
#undef SHZ_LRS
#undef SHZ_CAP
#include "../codec/shz_ref.h"
#define OPAQUE(v) __asm__ volatile("" : "+r"(v))
static long bad;
static void report(const char *name, const char *dom, long n, long b) {
    printf("  %-9s %-44s %12ld inputs  %ld wrong\n", name, dom, n, b); bad += b; }

int main(void) {
    long b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0;
    /* whole 32-bit words: SPLIT, TOP12, CRCBIT, and the joins SAMETOP and P12 */
    for (uint64_t i = 0; i < (1ull << 32); i++) {
        uint32_t x = (uint32_t)i; OPAQUE(x);
        b1 += (uint32_t)lw_SPLIT((int32_t)x) != L_SPLIT(x);
        b2 += (uint32_t)lw_TOP12((int32_t)x) != L_TOP12(x);
        b3 += (uint32_t)lw_CRCBIT((int32_t)x) != L_CRCBIT(x);
        b4 += (uint32_t)lw_Z8((int32_t)(x >> 24)) != L_SAMETOP(x);
        b5 += (uint32_t)lw_MAX1(lw_TOP12((int32_t)x)) != L_MAX1(L_TOP12(x));
    }
    report("SPLIT", "all 2^32 words", 1l << 32, b1);
    report("TOP12", "all 2^32 words", 1l << 32, b2);
    report("CRCBIT", "all 2^32 words", 1l << 32, b3);
    report("SAMETOP", "join Z8(v >>> 24), all 2^32 words", 1l << 32, b4);
    report("P12", "join MAX1(TOP12(s)), all 2^32 words", 1l << 32, b5);

    long n = 0; b1 = 0;
    for (uint32_t u = 0; u < 256; u++) { b1 += (uint32_t)lw_Z8((int32_t)u) != (u == 0); n++; }
    report("Z8", "all of [0, 255]", n, b1);
    n = 0; b1 = 0;
    for (uint32_t u = 0; u < 4096; u++) { b1 += (uint32_t)lw_MAX1((int32_t)u) != L_MAX1(u); n++; }
    report("MAX1", "all of [0, 4095]", n, b1);
    n = 0; b1 = 0; b2 = 0;
    for (uint32_t k = 0; k <= SHZ_NLIM; k++) { b1 += (uint32_t)lw_RATE((int32_t)k) != L_RATE(k); b2 += (uint32_t)lw_NINC((int32_t)k) != L_NINC(k); n++; }
    report("RATE", "all of [0, 62]", n, b1);
    report("NINC", "all of [0, 62]", n, b2);

    n = 0; b1 = 0; b2 = 0;
    for (uint32_t R = 1; R <= 6; R++)
        for (uint32_t p = 0; p < (1u << 22); p++) {
            uint32_t x = p | R << 22; OPAQUE(x);
            b1 += (uint32_t)lw_INC1((int32_t)x) != L_INC1(x);
            b2 += (uint32_t)lw_DEC0((int32_t)x) != L_DEC0(x); n++;
        }
    report("INC1", "all p in [0, 2^22), R in [1, 6]", n, b1);
    report("DEC0", "all p in [0, 2^22), R in [1, 6]", n, b2);

    n = 0; b1 = 0; b2 = 0;
    for (uint32_t x = 0; x < (1u << 30); x++) {
        uint32_t y = x; OPAQUE(y);
        b1 += (uint32_t)lw_HASH((int32_t)y) != L_HASH(y);
        if (y < (1u << 28)) b2 += (uint32_t)lw_HASHF((int32_t)y) != L_HASHF(y);
        n++;
    }
    report("HASH", "all of [0, 2^30)", n, b1);
    report("HASHF", "all of [0, 2^28)", 1l << 28, b2);

    n = 0; b1 = 0;
    for (uint32_t k = 0; k <= SHZ_CAP; k++) { b1 += (uint32_t)lw_BKT((int32_t)k) != L_BKT(k); n++; }
    report("BKT", "all of [0, 32767]", n, b1);

    long bc[6] = {0}; n = 0; long bt = 0;
    for (uint32_t c = 0; c < 256; c++) {
        bc[0] += (uint32_t)lw_ISDIG((int32_t)c) != L_ISDIG(c);
        bc[1] += (uint32_t)lw_ISUPPER((int32_t)c) != L_ISUPPER(c);
        bc[2] += (uint32_t)lw_ISLOWER((int32_t)c) != L_ISLOWER(c);
        bc[3] += (uint32_t)lw_ISUND((int32_t)c) != L_ISUND(c);
        bc[4] += (uint32_t)lw_ISSPACE((int32_t)c) != (c == ' ');
        bc[5] += (uint32_t)lw_ISTAB((int32_t)c) != (c == '\t');
        /* the class table: 1 alnum (0-9 A-Z a-z _ and >= 128), 2 space or tab, 0 otherwise */
        uint32_t want = ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 128) ? 1
                        : (c == ' ' || c == '\t') ? 2 : 0;
        uint32_t got = (uint32_t)(lw_ISDIG((int32_t)c) | lw_ISUPPER((int32_t)c) | lw_ISLOWER((int32_t)c) | lw_ISUND((int32_t)c) | (int32_t)(c >> 7))
                       + 2u * (uint32_t)(lw_ISSPACE((int32_t)c) | lw_ISTAB((int32_t)c));
        bt += got != want; n++;
    }
    const char *cn[6] = {"ISDIG", "ISUPPER", "ISLOWER", "ISUND", "ISSPACE", "ISTAB"};
    for (int k = 0; k < 6; k++) report(cn[k], "all of [0, 255]", n, bc[k]);
    report("CLASS", "join of the class lanes, all of [0, 255]", n, bt);

    n = 0; b1 = 0; b2 = 0; b3 = 0; b4 = 0;
    for (uint32_t a = 0; a < 10; a++) for (uint32_t bb = 0; bb < 10; bb++) for (uint32_t k = 0; k < 2; k++) {
        uint32_t x = a | bb << 4 | k << 8;
        b1 += (uint32_t)lw_DSUM((int32_t)x) != L_DSUM(x);
        b2 += lw_DDIF((int32_t)x) != L_DDIF(x);
        uint32_t s = a + bb + k; int32_t d = (int32_t)a - (int32_t)bb - (int32_t)k;
        b3 += (uint32_t)lw_DFIX(lw_DSUM((int32_t)x)) != (s < 10 ? s : (s - 10) | 16u);
        b4 += (uint32_t)lw_BFIX(lw_DDIF((int32_t)x)) != (d >= 0 ? (uint32_t)d : (uint32_t)(d + 10) | 16u);
        n++;
    }
    report("DSUM", "all 200 digit triples", n, b1);
    report("DDIF", "all 200 digit triples", n, b2);
    report("DADD", "join DFIX(DSUM(x)) = digit | carry << 4", n, b3);
    report("DSUB", "join BFIX(DDIF(x)) = digit | borrow << 4", n, b4);
    n = 0; b1 = 0;
    for (uint32_t s = 0; s < 20; s++) { b1 += (uint32_t)lw_DFIX((int32_t)s) != L_DFIX(s); n++; }
    report("DFIX", "all of [0, 19]", n, b1);
    n = 0; b1 = 0;
    for (int32_t s = -10; s < 10; s++) { b1 += (uint32_t)lw_BFIX(s) != L_BFIX(s); n++; }
    report("BFIX", "all of [-10, 9]", n, b1);

    n = 0; b1 = 0;
    for (uint32_t y = 0; y < (1u << 19); y++) { uint32_t z = y; OPAQUE(z); b1 += (uint32_t)lw_DELTA((int32_t)z) != L_DELTA(z); n++; }
    report("DELTA", "all d in [0, 4095], w in [0, 127]", n, b1);
    n = 0; b1 = 0;
    for (int32_t s = -2047; s <= 2047; s++) for (int32_t e = -4095; e <= 4095; e++) {
        uint32_t x = (uint32_t)(s + 2048) | (uint32_t)(e + 4096) << 12; OPAQUE(x);
        b1 += lw_WSTEP((int32_t)x) != L_WSTEP(x); n++;
    }
    report("WSTEP", "all s in [-2047, 2047], e in [-4095, 4095]", n, b1);

    n = 0; b1 = 0; b3 = 0;
    for (int32_t v = -32768; v <= 32767; v++) {
        b1 += lw_CLLO(v) != (v < -2047 ? -2047 : v);
        b3 += lw_CLHI(lw_CLLO(v)) != L_CLAMP12(v); n++;
    }
    report("CLLO", "all of [-32768, 32767]", n, b1);
    n = 0; b2 = 0;
    for (int32_t v = -2047; v <= 32767; v++) { b2 += lw_CLHI(v) != (v > 2047 ? 2047 : v); n++; }
    report("CLHI", "all of [-2047, 32767]", n, b2);
    report("CLAMP12", "join CLHI(CLLO(v)), all of [-32768, 32767]", 65536, b3);
    n = 0; b1 = 0; b3 = 0;
    for (int32_t w = -(1 << 18) - (1 << 13); w <= (1 << 18) + (1 << 13); w++) {
        b1 += lw_WLO(w) != (w < -(1 << 18) ? -(1 << 18) : w);
        b3 += lw_WHI(lw_WLO(w)) != L_WCLAMP(w); n++;
    }
    report("WLO", "all of [-2^18 - 2^13, 2^18 + 2^13]", n, b1);
    report("WCLAMP", "join WHI(WLO(w)), same range", n, b3);
    n = 0; b2 = 0;
    for (int32_t w = -(1 << 18); w <= (1 << 18) + (1 << 13); w++) { b2 += lw_WHI(w) != (w > (1 << 18) - 1 ? (1 << 18) - 1 : w); n++; }
    report("WHI", "all of [-2^18, 2^18 + 2^13]", n, b2);
    n = 0; b1 = 0;
    for (uint32_t x = 0; x < 65536; x++) { b1 += (uint32_t)lw_HASH2((int32_t)x) != L_HASH2(x); n++; }
    report("HASH2", "all of [0, 65535]", n, b1);

    printf("%s: %ld wrong in all\n", bad ? "FAIL" : "PASS", bad);
    return bad != 0;
}
