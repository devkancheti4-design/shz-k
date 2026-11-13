/* shzx_ref.h -- the MEANINGS of the max mode's new integer laws (shzx_laws.h holds them as authored; a build with
   -DSHZ_REF must give byte-identical streams).  The max mode also uses shz_ref.h's laws: RATE and NINC (counts in
   [0, 62]), BKT, MAX1, WSTEP, WCLAMP, CLAMP12 (here on sums in [-2^17, 2^17]), ISUPPER, ISLOWER. */
#ifndef SHZX_REF_H
#define SHZX_REF_H
#include <stdint.h>
/* HM1: a context hash, any word: (x ^ (x >>> 15)) * 0x2C1B3C6D, wrapping */
static inline uint32_t L_HM1(uint32_t x) { return (x ^ (x >> 15)) * 0x2C1B3C6Du; }
/* INC16 / DEC16: a 16-bit probability's step toward 1 / toward 0, x = p | R << 16, p in [0, 65535], R in [1, 7] */
static inline uint32_t L_INC16(uint32_t x) { return (65535u - (x & 0xFFFFu)) >> (x >> 16); }
static inline uint32_t L_DEC16(uint32_t x) { return (x & 0xFFFFu) >> (x >> 16); }
/* SDELTA: signed interpolation step of an APM, y = (d + 4096) | w << 13, d in [-4095, 4095], w in [0, 127]:
   (d * w + 64) >> 7, arithmetic */
static inline uint32_t L_SDELTA(uint32_t y) {
    int32_t d = (int32_t)(y & 8191u) - 4096, w = (int32_t)(y >> 13); return (uint32_t)((d * w + 64) >> 7); }

#endif
