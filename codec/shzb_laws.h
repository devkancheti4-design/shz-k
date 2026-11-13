/* shzb_laws.h -- the bytes law's integer laws (decimal digits of a byte value), machine-authored by integer program
 * synthesis (each from its own search, verbatim; compile with -fwrapv). Proven on all of [0, 255] by proofs/bprove.c. */
#ifndef SHZB_LAWS_H
#define SHZB_LAWS_H
#include <stdint.h>
/* BDIV100: the hundreds digit of a byte value: b / 100, b in [0, 255]
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_BDIV100(int32_t x) { return (((int)((unsigned)(x) * (unsigned)(41))) >> (12 & 31)); }
/* BDIV10: a byte value divided by ten: b / 10, b in [0, 255]
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_BDIV10(int32_t x) { return (((int)((unsigned)(x) * (unsigned)(205))) >> (11 & 31)); }
/* BMOD10: the last decimal digit of a byte value: b % 10, b in [0, 255]
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_BMOD10(int32_t x) { return ((x + (((int)((unsigned)(x) * (unsigned)(205))) >> (11 & 31))) - ((int)((unsigned)(11) * (unsigned)((((int)((unsigned)(x) * (unsigned)(205))) >> (11 & 31)))))); }
#ifndef SHZ_LANES_ONLY
static inline uint32_t L_BDIV100(uint32_t b) { return (uint32_t)lw_BDIV100((int32_t)b); }
static inline uint32_t L_BDIV10(uint32_t b)  { return (uint32_t)lw_BDIV10((int32_t)b); }
static inline uint32_t L_BMOD10(uint32_t b)  { return (uint32_t)lw_BMOD10((int32_t)b); }
#endif
#endif
