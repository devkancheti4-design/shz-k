/* shzb_ref.h -- meanings of shzb's new integer laws (shzb_laws.h holds them as authored): decimal digits of a byte */
#ifndef SHZB_REF_H
#define SHZB_REF_H
#include <stdint.h>
static inline uint32_t L_BDIV100(uint32_t b) { return b / 100; }
static inline uint32_t L_BDIV10(uint32_t b)  { return b / 10; }
static inline uint32_t L_BMOD10(uint32_t b)  { return b % 10; }
#endif
