/* shza_laws.h -- the AI-transcript mode's new integer laws, machine-authored by integer program synthesis (each from
 * its own search, verbatim; compile with -fwrapv). Proven on all 2^32 words by proofs/aprove.c. */
#ifndef SHZA_LAWS_H
#define SHZA_LAWS_H
#include <stdint.h>
/* AORD32: a float's bits as an unsigned integer that sorts like the float: u ^ (u < 0 ? 0xFFFFFFFF : 0x80000000)
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_AORD32(int32_t x) { return ((x >> 31) ^ (x | -2147483648)); }
/* AUNORD32: the inverse: o ^ (o top bit set ? 0x80000000 : 0xFFFFFFFF)
 * machine-authored by integer program synthesis (size class 2) */
static inline int32_t lw_AUNORD32(int32_t x) { return ((x ^ (~-2147483648)) ^ (-2147483648 | (x >> 31))); }
#ifndef SHZ_LANES_ONLY
static inline uint32_t L_ORD32(uint32_t u)   { return (uint32_t)lw_AORD32((int32_t)u); }
static inline uint32_t L_UNORD32(uint32_t o) { return (uint32_t)lw_AUNORD32((int32_t)o); }
#endif
#endif
