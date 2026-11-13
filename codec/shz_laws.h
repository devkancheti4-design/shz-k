/* shz_laws.h -- every integer law of the shz codec, machine-authored by integer program synthesis: each lane a
 * synthesised expression from its own search, written here verbatim as an int32 function (compile with
 * -fwrapv). Proven on every input of each lane's domain by proofs/prove.c.  */
#ifndef SHZ_LAWS_H
#define SHZ_LAWS_H
#include <stdint.h>

#define SHZ_NLIM 62     /* a counter's update count saturates here */
#define SHZ_LRS 11      /* the mixer's learning rate */
#define SHZ_CAP 32767   /* runs and match lengths are capped here */

/* SPLIT: the coder's split offset: (x & 0xFFFFF) * (x >>> 20), x = (range >> 12) | p << 20
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_SPLIT(int32_t x) { return ((int)((unsigned)((x & 1048575)) * (unsigned)((4095 & (x >> 20))))); }
/* Z8: is the byte zero: 1 if u == 0, u in [0, 255]  (SAMETOP(v) = Z8(v >>> 24): the top bytes of x1, x2 agree)
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_Z8(int32_t x) { return (1 & ((x - 1) >> 31)); }
/* TOP12: a counter's top 12 bits: s >>> 20
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_TOP12(int32_t x) { return (4095 & (x >> 20)); }
/* MAX1: max(u, 1) for u in [0, 4095]: the coder never sees probability 0
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_MAX1(int32_t x) { return (x - ((x - 1) >> 31)); }
/* RATE: a counter's rate shift after n updates: floor(log2(n + 2)), n in [0, 62]
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_RATE(int32_t x) { return (31 - (__builtin_clz((unsigned)((x + 2)) | 1u) + (((x + 2)) == 0))); }
/* INC1: a step toward 1: (2^22 - 1 - p) >> R, x = p | R << 22, p in [0, 2^22), R in [1, 6]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_INC1(int32_t x) { return ((4194303 & (~x)) >> ((x >> 22) & 31)); }
/* DEC0: a step toward 0: p >> R, same x
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_DEC0(int32_t x) { return ((x & 4194303) >> ((x >> 22) & 31)); }
/* NINC: a counter's update count, saturating: min(n + 1, 62), n in [0, 62]
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_NINC(int32_t x) { return (x - ((x - 62) >> 31)); }
/* HASH: the match context hash: ((h * 24) + c + 1) mod 2^22, x = h | c << 22, h < 2^22, c < 256
 * machine-authored by integer program synthesis (size class 5) */
static inline int32_t lw_HASH(int32_t x) { return (4194303 & ((x >> 22) + (((int)((unsigned)(x) * (unsigned)(24))) + 1))); }
/* HASHF: field recall hash: ((h * 24) + c + 1) mod 2^20, x = h | c << 20, h < 2^20, c < 256
 * machine-authored by integer program synthesis (size class 5) */
static inline int32_t lw_HASHF(int32_t x) { return (1048575 & ((x >> 20) + (((int)((unsigned)(x) * (unsigned)(24))) + 1))); }
/* BKT: how sure a hallucinator is: the bit length of its run n in [0, 32767]
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_BKT(int32_t x) { return (32 - (__builtin_clz((unsigned)(x) | 1u) + ((x) == 0))); }
/* ISDIG: is c a decimal digit: 1 if 48 <= c <= 57, c in [0, 255]
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_ISDIG(int32_t x) { return (((x - 48) >> 31) - ((x - 58) >> 31)); }
/* ISUPPER: is c an upper-case letter: 1 if 65 <= c <= 90, c in [0, 255]
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_ISUPPER(int32_t x) { return (((x - 65) >> 31) - ((x - 91) >> 31)); }
/* ISLOWER: is c a lower-case letter: 1 if 97 <= c <= 122, c in [0, 255]
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_ISLOWER(int32_t x) { return (((x - 97) >> 31) - ((x - 123) >> 31)); }
/* ISUND: is c an underscore: 1 if c == 95, c in [0, 255]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_ISUND(int32_t x) { return (1 & (((x ^ 95) - 1) >> 31)); }
/* ISSPACE: is c a space: 1 if c == 32, c in [0, 255]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_ISSPACE(int32_t x) { return (1 & (((x ^ 32) - 1) >> 31)); }
/* ISTAB: is c a tab: 1 if c == 9, c in [0, 255]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_ISTAB(int32_t x) { return (1 & (((x ^ 9) - 1) >> 31)); }
/* DSUM: a + b + k for x = a | b << 4 | k << 8 (the numeric hallucination adds digit by digit)
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_DSUM(int32_t x) { return ((15 & (x >> 4)) + (15 & (x + (x >> 8)))); }
/* DFIX: digit | carry << 4 of a digit sum s in [0, 19]: s < 10 ? s : s + 6
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_DFIX(int32_t x) { return (x + (6 & ((9 - x) >> 31))); }
/* DDIF: a - b - k for x = a | b << 4 | k << 8
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_DDIF(int32_t x) { return (((x & 15) - (x >> 8)) - (15 & (x >> 4))); }
/* BFIX: digit | borrow << 4 of a digit difference s in [-10, 9]: s >= 0 ? s : s + 26
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_BFIX(int32_t x) { return (x + (26 & (x >> 31))); }
/* DELTA: squash interpolation: (d * w + 64) >> 7, y = d | w << 12, d in [0, 4095], w in [0, 127]
 * machine-authored by integer program synthesis (size class 5) */
static inline int32_t lw_DELTA(int32_t x) { return ((((int)((unsigned)((x >> 12)) * (unsigned)((x & 4095)))) >> 7) + ((4095 + (64 & ((int)((unsigned)(x) * (unsigned)((x >> 12)))))) >> 12)); }
/* WSTEP: a mixer weight's step: (s * e) >> 11, x = (s + 2048) | (e + 4096) << 12, s in [-2047, 2047], e in [-4095, 4095]
 * machine-authored by integer program synthesis (size class 5) */
static inline int32_t lw_WSTEP(int32_t x) { return ((4096 - ((x & 4095) - ((int)((unsigned)(4095) * (unsigned)((x >> 12)))))) - (x - (((int)((unsigned)((x >> 12)) * (unsigned)((x & 4095)))) >> 11))); }
/* CLLO: the mixer's sum, floor: max(v, -2047), v in [-32768, 32767]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_CLLO(int32_t x) { return ((-2047 & ((x - -2047) >> 31)) + (x - (x & ((x - -2047) >> 31)))); }
/* CLHI: the mixer's sum, ceiling: min(v, 2047), v in [-2047, 32767]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_CLHI(int32_t x) { return ((2047 & ((2047 - x) >> 31)) + (x - (x & ((2047 - x) >> 31)))); }
/* WLO: a weight, floor: max(w, -2^18), w in [-2^18 - 2^13, 2^18 + 2^13]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_WLO(int32_t x) { return ((-262144 & ((x - -262144) >> 31)) + (x - (x & ((x - -262144) >> 31)))); }
/* WHI: a weight, ceiling: min(w, 2^18 - 1), w in [-2^18, 2^18 + 2^13]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_WHI(int32_t x) { return ((262143 & ((262143 - x) >> 31)) + (x - (x & ((262143 - x) >> 31)))); }
/* HASH2: the order-2 literal row: ((x * 2654435761) mod 2^32) >> 20, x = c2 | c1 << 8 in [0, 65535]
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_HASH2(int32_t x) { return (4095 & (((int)((unsigned)(x) * (unsigned)(-1640531535))) >> 20)); }
/* CRCBIT: one bit step of CRC-32 (IEEE, reflected): (c >>> 1) ^ (0xEDB88320 if c is odd)
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_CRCBIT(int32_t x) { return ((2147483647 & (x >> 1)) ^ (-306674912 & (-(x & 1)))); }

#ifndef SHZ_LANES_ONLY
/* the names the codec calls: one lane each, or a fixed join of lanes */
static inline uint32_t L_SPLIT(uint32_t x)   { return (uint32_t)lw_SPLIT((int32_t)x); }
static inline uint32_t L_SAMETOP(uint32_t v) { return (uint32_t)lw_Z8((int32_t)(v >> 24)); }
static inline uint32_t L_TOP12(uint32_t s)   { return (uint32_t)lw_TOP12((int32_t)s); }
static inline uint32_t L_MAX1(uint32_t u)    { return (uint32_t)lw_MAX1((int32_t)u); }
static inline uint32_t L_RATE(uint32_t n)    { return (uint32_t)lw_RATE((int32_t)n); }
static inline uint32_t L_INC1(uint32_t x)    { return (uint32_t)lw_INC1((int32_t)x); }
static inline uint32_t L_DEC0(uint32_t x)    { return (uint32_t)lw_DEC0((int32_t)x); }
static inline uint32_t L_NINC(uint32_t n)    { return (uint32_t)lw_NINC((int32_t)n); }
static inline uint32_t L_HASH(uint32_t x)    { return (uint32_t)lw_HASH((int32_t)x); }
static inline uint32_t L_HASHF(uint32_t x)   { return (uint32_t)lw_HASHF((int32_t)x); }
static inline uint32_t L_BKT(uint32_t n)     { return (uint32_t)lw_BKT((int32_t)n); }
static inline uint32_t L_ISDIG(uint32_t c)   { return (uint32_t)lw_ISDIG((int32_t)c); }
static inline uint32_t L_ISUPPER(uint32_t c) { return (uint32_t)lw_ISUPPER((int32_t)c); }
static inline uint32_t L_ISLOWER(uint32_t c) { return (uint32_t)lw_ISLOWER((int32_t)c); }
static inline uint32_t L_ISUND(uint32_t c)   { return (uint32_t)lw_ISUND((int32_t)c); }
static inline uint32_t L_ISSP(uint32_t c)    { return (uint32_t)(lw_ISSPACE((int32_t)c) | lw_ISTAB((int32_t)c)); }
static inline uint32_t L_DSUM(uint32_t x)    { return (uint32_t)lw_DSUM((int32_t)x); }
static inline uint32_t L_DFIX(uint32_t s)    { return (uint32_t)lw_DFIX((int32_t)s); }
static inline int32_t  L_DDIF(uint32_t x)    { return lw_DDIF((int32_t)x); }
static inline uint32_t L_BFIX(int32_t s)     { return (uint32_t)lw_BFIX(s); }
static inline uint32_t L_DELTA(uint32_t y)   { return (uint32_t)lw_DELTA((int32_t)y); }
static inline int32_t  L_WSTEP(uint32_t x)   { return lw_WSTEP((int32_t)x); }
static inline int32_t  L_CLAMP12(int32_t v)  { return lw_CLHI(lw_CLLO(v)); }
static inline int32_t  L_WCLAMP(int32_t w)   { return lw_WHI(lw_WLO(w)); }
static inline uint32_t L_HASH2(uint32_t x)   { return (uint32_t)lw_HASH2((int32_t)x); }
static inline uint32_t L_CRCBIT(uint32_t c)  { return (uint32_t)lw_CRCBIT((int32_t)c); }
#endif
#endif
