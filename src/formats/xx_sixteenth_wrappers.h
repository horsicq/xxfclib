/* SPDX-License-Identifier: MIT. Sixteenth owned bounded protocol primitives. */
#ifndef XX_SIXTEENTH_WRAPPERS_H
#define XX_SIXTEENTH_WRAPPERS_H
#include "xx_fifteenth_wrappers.h"
static uint16_t f16_u16(const uint8_t *p,bool le){return le?pm_le16(p):pm_be16(p);}
static uint32_t f16_u32(const uint8_t *p,bool le){return le?pm_le32(p):pm_be32(p);}
static uint64_t f16_seq(const uint8_t *p,bool le){return ((uint64_t)f16_u32(p,le)<<32)|f16_u32(p+4,le);}
static bool f16_ip4(const uint8_t *p,bool multicast){uint32_t v=pm_be32(p);return multicast?(v>=0xe0000000U&&v<0xf0000000U):(v>0&&v<0xe0000000U&&(v>>24)!=127);}
static bool f16_mask(uint32_t m){uint32_t inverted=~m;return (inverted&(inverted+1))==0;}
#endif
