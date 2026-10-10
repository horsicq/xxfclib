/* SPDX-License-Identifier: MIT. owned bounded protocol primitives. */
#ifndef XX_NETWORK_FIELDS_H
#define XX_NETWORK_FIELDS_H
#include "xx_network_packet.h"
static XXFC_MAYBE_UNUSED uint64_t network_seq(const uint8_t *p, bool le)
{
    return ((uint64_t)xx_data_get_u32(p, 4, 0, !le) << 32) | xx_data_get_u32(p + 4, 4, 0, !le);
}
static XXFC_MAYBE_UNUSED bool network_ip4(const uint8_t *p, bool multicast)
{
    uint32_t v = xx_data_get_u32(p, 4, 0, true);
    return multicast ? (v >= 0xe0000000U && v < 0xf0000000U) : (v > 0 && v < 0xe0000000U && (v >> 24) != 127);
}
static XXFC_MAYBE_UNUSED bool network_mask(uint32_t m)
{
    uint32_t inverted = ~m;
    return (inverted & (inverted + 1)) == 0;
}
#endif
