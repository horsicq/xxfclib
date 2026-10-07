/* SPDX-License-Identifier: MIT. Sixteenth owned bounded protocol primitives. */
#ifndef XX_SIXTEENTH_WRAPPERS_H
#define XX_SIXTEENTH_WRAPPERS_H
#include "xx_fifteenth_wrappers.h"
static XXFC_MAYBE_UNUSED uint64_t f16_seq(const uint8_t *p,bool le){return ((uint64_t)xx_data_get_u32(p, 4, 0, !le)<<32)|xx_data_get_u32(p+4, 4, 0, !le);}
static XXFC_MAYBE_UNUSED bool f16_ip4(const uint8_t *p,bool multicast){uint32_t v=xx_data_get_u32(p, 4, 0, true);return multicast?(v>=0xe0000000U&&v<0xf0000000U):(v>0&&v<0xe0000000U&&(v>>24)!=127);}
static XXFC_MAYBE_UNUSED bool f16_mask(uint32_t m){uint32_t inverted=~m;return (inverted&(inverted+1))==0;}
#endif
