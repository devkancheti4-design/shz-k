/* shza_ref.h -- the MEANINGS of the AI-transcript mode's two new integer laws (shza_laws.h holds them as authored).
   ORD32 maps a float's 32 bits to an unsigned integer that sorts like the float (negative floats: all bits flipped;
   positive: the sign bit set), so neighbouring scores have neighbouring codes; UNORD32 is its inverse. */
#ifndef SHZA_REF_H
#define SHZA_REF_H
#include <stdint.h>
static inline uint32_t L_ORD32(uint32_t u)   { return u ^ ((u >> 31) ? 0xFFFFFFFFu : 0x80000000u); }
static inline uint32_t L_UNORD32(uint32_t o) { return o ^ ((o >> 31) ? 0x80000000u : 0xFFFFFFFFu); }
#endif
