/* SPDX-License-Identifier: MIT. Private container primitives. */
#ifndef XX_CONTAINER_WIRE_HELPERS_H
#define XX_CONTAINER_WIRE_HELPERS_H
#include "xx_serialized_value_helpers.h"

static XXFC_MAYBE_UNUSED bool container_wire_z(memory_blob *b, uint64_t *at, uint64_t end, bool utf) {
    uint64_t start = *at;
    while (*at < end && b->p[(size_t)*at]) {
        if (!blob_span(b, *at, 1) || *at - start > 65536)
            return false;
        ++*at;
    }
    if (*at == end || (utf && !serialized_utf(b, start, *at - start)))
        return false;
    ++*at;
    return true;
}
static uint32_t container_wire_unmask(uint32_t c) {
    c -= 0xa282ead8U;
    return (c >> 17) | (c << 15);
}
static XXFC_MAYBE_UNUSED bool container_wire_crc(memory_blob *b, uint64_t at, uint64_t n, uint32_t c) {
    return serialized_crc(b, at, n, container_wire_unmask(c), true);
}
static XXFC_MAYBE_UNUSED bool container_wire_zero(memory_blob *b, uint64_t at, uint64_t n) {
    if (!blob_span(b, at, n))
        return false;
    for (uint64_t i = 0; i < n; ++i) {
        if (!(i & 65535U) && binary_stop(b->pd))
            return false;
        if (b->p[(size_t)(at + i)])
            return false;
    }
    return true;
}
#endif
