/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_cramfs_direct_native.h"

#include <limits.h>

#define XX_CRAMFS_DIRECT_POINTER UINT32_C(0x40000000)
#define XX_CRAMFS_UNCOMPRESSED_POINTER UINT32_C(0x80000000)
#define XX_CRAMFS_POINTER_OFFSET_MASK UINT32_C(0x3fffffff)
#define XX_CRAMFS_BLOCK_SIZE 4096U

bool xx_cramfs_direct_native_span(uint32_t pointer, int64_t base, int64_t image_end, size_t expected, int64_t *start, int64_t *next)
{
    uint64_t relative;
    int64_t absolute;
    if (!start || !next || (pointer & (XX_CRAMFS_DIRECT_POINTER | XX_CRAMFS_UNCOMPRESSED_POINTER)) != (XX_CRAMFS_DIRECT_POINTER | XX_CRAMFS_UNCOMPRESSED_POINTER) ||
        base < 0 || image_end <= base || expected == 0U || expected > XX_CRAMFS_BLOCK_SIZE) {
        return false;
    }
    relative = (uint64_t)(pointer & XX_CRAMFS_POINTER_OFFSET_MASK) << 2;
    if (relative > (uint64_t)(INT64_MAX - base)) return false;
    absolute = base + (int64_t)relative;
    if (absolute < base || absolute > image_end || (uint64_t)expected > (uint64_t)(image_end - absolute)) {
        return false;
    }
    *start = absolute;
    *next = absolute + (int64_t)expected;
    return true;
}
