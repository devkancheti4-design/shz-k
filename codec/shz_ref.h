/* shz_ref.h -- the MEANINGS of the codec's integer laws, written plainly.
   shz_laws.h holds the same laws as authored by integer program synthesis; a build with -DSHZ_REF must give
   byte-identical compressed streams.  Each law is exact on the domain stated beside it, and the codec only ever
   calls it inside that domain (law/prove.c checks every law on its whole domain against these meanings). */
#ifndef SHZ_REF_H
#define SHZ_REF_H
#include <stdint.h>

#define SHZ_NLIM 62     /* a counter's update count saturates here */
#define SHZ_LRS 11      /* the mixer's learning rate: (stretch * err) >> 11 */
#define SHZ_CAP 32767   /* runs and match lengths are capped here */

static inline uint32_t clz_(uint32_t v) { return v ? (uint32_t)__builtin_clz(v) : 32u; }

/* ---- the arithmetic coder ---- */
/* SPLIT: the split point's offset, x = (range >> 12) | p << 20 (any word): (x & 0xFFFFF) * (x >>> 20) */
static inline uint32_t L_SPLIT(uint32_t x) { return (x & 0xFFFFFu) * (x >> 20); }
/* SAMETOP: 1 while the top bytes of x1 and x2 agree, v = x1 ^ x2 (any word) */
static inline uint32_t L_SAMETOP(uint32_t v) { return (v >> 24) == 0; }

/* ---- adaptive counters: s = p22 << 10 | n ---- */
/* TOP12: the probability's top 12 bits (any word) */
static inline uint32_t L_TOP12(uint32_t s) { return s >> 20; }
/* MAX1: max(u, 1), u in [0, 4095]  (P12 = MAX1(TOP12(s)), the coder's probability, never 0) */
static inline uint32_t L_MAX1(uint32_t u) { return u ? u : 1; }
/* RATE: the shift for a counter that has seen n updates: floor(log2(n + 2)), n in [0, 62]  (1/2 ... 1/64) */
static inline uint32_t L_RATE(uint32_t n) { return 31u - clz_(n + 2); }
/* INC1: a step toward 1, x = p | R << 22, p in [0, 2^22), R in [1, 6]: (2^22 - 1 - p) >> R */
static inline uint32_t L_INC1(uint32_t x) { return (0x3FFFFFu - (x & 0x3FFFFFu)) >> (x >> 22); }
/* DEC0: a step toward 0, same x: p >> R */
static inline uint32_t L_DEC0(uint32_t x) { return (x & 0x3FFFFFu) >> (x >> 22); }
/* NINC: the count, saturating: min(n + 1, 62), n in [0, 62] */
static inline uint32_t L_NINC(uint32_t n) { return n < SHZ_NLIM ? n + 1 : SHZ_NLIM; }

/* ---- the hallucinators' keys ---- */
/* HASH: the match model's rolling context hash, x = h | c << 22, h < 2^22, c < 256 */
static inline uint32_t L_HASH(uint32_t x) { return ((x & 0x3FFFFFu) * 24u + (x >> 22) + 1u) & 0x3FFFFFu; }
/* HASHF: field recall's rolling hash of (field, token prefix), x = h | c << 20, h < 2^20, c < 256 */
static inline uint32_t L_HASHF(uint32_t x) { return ((x & 0xFFFFFu) * 24u + (x >> 20) + 1u) & 0xFFFFFu; }
/* BKT: how sure a hallucinator is, from its run of right guesses (or match length) n in [0, 32767]: bit length of n */
static inline uint32_t L_BKT(uint32_t n) { return 32u - clz_(n); }

/* ---- token classes, c in [0, 255] ---- */
static inline uint32_t L_ISDIG(uint32_t c)   { return c >= '0' && c <= '9'; }
static inline uint32_t L_ISUPPER(uint32_t c) { return c >= 'A' && c <= 'Z'; }
static inline uint32_t L_ISLOWER(uint32_t c) { return c >= 'a' && c <= 'z'; }
static inline uint32_t L_ISUND(uint32_t c)   { return c == '_'; }
static inline uint32_t L_ISSP(uint32_t c)    { return c == ' ' || c == '\t'; }

/* ---- decimal digits for the numeric hallucination: x = a | b << 4 | k << 8, a, b in [0, 9], k in {0, 1} ---- */
/* DSUM: a + b + k */
static inline uint32_t L_DSUM(uint32_t x) { return (x & 15u) + ((x >> 4) & 15u) + (x >> 8); }
/* DFIX: the digit and the carry of a sum s in [0, 19]: s < 10 ? s : (s - 10) | 16 */
static inline uint32_t L_DFIX(uint32_t s) { return s < 10 ? s : s + 6; }
/* DDIF: a - b - k (signed) */
static inline int32_t L_DDIF(uint32_t x) { return (int32_t)(x & 15u) - (int32_t)((x >> 4) & 15u) - (int32_t)(x >> 8); }
/* BFIX: the digit and the borrow of a difference s in [-10, 9]: s >= 0 ? s : (s + 10) | 16 */
static inline uint32_t L_BFIX(int32_t s) { return (uint32_t)(s >= 0 ? s : s + 26); }

/* ---- the mixer ---- */
/* DELTA: interpolation between two squash table points, y = d | w << 12, d in [0, 4095], w in [0, 127]:
   (d * w + 64) >> 7   (squash(x) = t[j] + DELTA((t[j+1] - t[j]) | w << 12), built once into a table) */
static inline uint32_t L_DELTA(uint32_t y) { return ((y & 4095u) * (y >> 12) + 64u) >> 7; }
/* WSTEP: a weight's step, x = (s + 2048) | (e + 4096) << 12, s in [-2047, 2047], e in [-4095, 4095]: (s * e) >> 11 */
static inline int32_t L_WSTEP(uint32_t x) {
    int32_t s = (int32_t)(x & 4095u) - 2048, e = (int32_t)(x >> 12) - 4096; return (s * e) >> SHZ_LRS; }
/* CLAMP12: the mixer's sum back in the stretched range, v in [-32768, 32767]: clamp to [-2047, 2047] */
static inline int32_t L_CLAMP12(int32_t v) { return v < -2047 ? -2047 : v > 2047 ? 2047 : v; }
/* WCLAMP: a weight kept in [-2^18, 2^18 - 1], w in [-2^18 - 2^13, 2^18 + 2^13] */
static inline int32_t L_WCLAMP(int32_t w) { return w < -(1 << 18) ? -(1 << 18) : w > (1 << 18) - 1 ? (1 << 18) - 1 : w; }
/* HASH2: the order-2 literal context row, x = c2 | c1 << 8 in [0, 65535]: (x * 2654435761) >>> 20, low 12 bits */
static inline uint32_t L_HASH2(uint32_t x) { return ((x * 2654435761u) >> 20) & 4095u; }

/* ---- the container ---- */
/* CRCBIT: one bit step of CRC-32 (IEEE, reflected), any word */
static inline uint32_t L_CRCBIT(uint32_t c) { return (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
#endif
