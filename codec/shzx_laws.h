/* shzx_laws.h -- the max mode's new integer laws, machine-authored by integer program synthesis (each lane from its
 * own search, verbatim, as an int32 function; compile with -fwrapv). Proven on every input of each lane's
 * domain by proofs/xprove.c.  */
#ifndef SHZX_LAWS_H
#define SHZX_LAWS_H
#include <stdint.h>

/* XHM1: a context hash: (x ^ (x >>> 15)) * 0x2C1B3C6D, wrapping (any word)
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_XHM1(int32_t x) { return ((int)((unsigned)(739982445) * (unsigned)((x ^ (131071 & (x >> 15)))))); }
/* XINC16: a 16-bit probability step toward 1: (65535 - p) >> R, x = p | R << 16, p in [0, 65535], R in [1, 7]
 * machine-authored by integer program synthesis (size class 4) */
static inline int32_t lw_XINC16(int32_t x) { return ((65535 & (~x)) >> ((x >> 16) & 31)); }
/* XDEC16: a 16-bit probability step toward 0: p >> R, same x
 * machine-authored by integer program synthesis (size class 3) */
static inline int32_t lw_XDEC16(int32_t x) { return ((x & 65535) >> ((x >> 16) & 31)); }
/* XSDELTA: an APM's signed interpolation step: (d * w + 64) >> 7, y = (d + 4096) | w << 13, d in [-4095, 4095], w in [0, 127]
 * machine-authored by integer program synthesis (size class 5) */
static inline int32_t lw_XSDELTA(int32_t x) { return (((64 + ((int)((unsigned)((x + 4096)) * (unsigned)((x >> 13))))) >> 7) - ((int)((unsigned)(64) * (unsigned)(((int)((unsigned)((x >> 13)) * (unsigned)(((x + 8191) >> 13)))))))); }

#ifndef SHZ_LANES_ONLY
static inline uint32_t L_HM1(uint32_t x)    { return (uint32_t)lw_XHM1((int32_t)x); }
static inline uint32_t L_INC16(uint32_t x)  { return (uint32_t)lw_XINC16((int32_t)x); }
static inline uint32_t L_DEC16(uint32_t x)  { return (uint32_t)lw_XDEC16((int32_t)x); }
static inline uint32_t L_SDELTA(uint32_t y) { return (uint32_t)lw_XSDELTA((int32_t)y); }
#endif
#endif
