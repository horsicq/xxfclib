/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_IS_STORED_POSITIONAL_H
#define XX_IS_STORED_POSITIONAL_H
#include "xxfclib/data/xx_data.h"
/* This corpus variant wraps an entire PE32 file without adding a header.
 * Its transform was recovered from repeated zero fields, the DOS stub and
 * the PE headers, then verified by the complete original PE checksum and
 * independent CAB decompression. No unknown key or period is searched. */
static bool is_positional_magic(const uint8_t *h)
{
    static const uint8_t sig[12] = {0xe0, 0xb8, 0x9f, 0xd9, 0xc9, 0xc1, 0xcd, 0xc9, 0xe5, 0xf1, 0xfd, 0xf9};
    return !xx_rt_memcmp(h, sig, sizeof(sig));
}
static void is_positional_decode(uint8_t *p, size_t n, uint64_t at)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned x = p[i] ^ 0xd5U;
        p[i] = (uint8_t)(((x >> 2U) | (x << 6U)) - (unsigned)((at + i) % 71U));
    }
}
static bool is_positional_read(Abstractformat *f, int64_t at, uint8_t *p, size_t n)
{
    if (!pm_read(f, at, p, n)) return false;
    is_positional_decode(p, n, (uint64_t)at);
    return true;
}
static bool is_positional_valid(Abstractformat *f, xx_pd_struct *pd)
{
    uint8_t h[128], section[40], block[4096];
    int64_t n = pm_available(f), at;
    uint32_t pe, headers, declared, sum = 0, check_at;
    unsigned count, optional, i;
    if (n < 512 || n > INT64_C(512) * 1024 * 1024 || xx_pd_is_stopped(pd) || !is_positional_read(f, 0, h, 64)) return false;
    pe = xx_data_get_u32(h + 60, 4, 0, false);
    if (h[0] != 'M' || h[1] != 'Z' || pe < 64 || pe > 1048576U || (pe & 3U) || (int64_t)pe > n - 128 || !is_positional_read(f, pe, h, sizeof(h))) return false;
    count = xx_data_get_u16(h + 6, 2, 0, false);
    optional = xx_data_get_u16(h + 20, 2, 0, false);
    if (xx_rt_memcmp(h, "PE\0\0", 4) || xx_data_get_u16(h + 4, 2, 0, false) != 0x14cU || !count || count > 96 || optional < 96 || optional > 4096 ||
        xx_data_get_u16(h + 24, 2, 0, false) != 0x10bU)
        return false;
    headers = xx_data_get_u32(h + 84, 4, 0, false);
    declared = xx_data_get_u32(h + 88, 4, 0, false);
    check_at = pe + 88;
    at = (int64_t)pe + 24 + optional;
    if (!declared || headers < at + (int64_t)count * 40 || headers > (uint64_t)n || at + (int64_t)count * 40 > n) return false;
    for (i = 0; i < count; i++, at += 40) {
        uint32_t size, offset;
        if (xx_pd_is_stopped(pd) || !is_positional_read(f, at, section, sizeof(section))) return false;
        size = xx_data_get_u32(section + 16, 4, 0, false);
        offset = xx_data_get_u32(section + 20, 4, 0, false);
        if (size && (offset < headers || offset > (uint64_t)n || size > (uint64_t)n - offset)) return false;
    }
    for (at = 0; at < n;) {
        size_t j, take = n - at > (int64_t)sizeof(block) ? sizeof(block) : (size_t)(n - at);
        if (xx_pd_is_stopped(pd) || !is_positional_read(f, at, block, take)) return false;
        for (j = 0; j < take; j += 2) {
            uint32_t word = block[j];
            if (j + 1 < take) word |= (uint32_t)block[j + 1] << 8U;
            if ((uint64_t)at + j >= check_at && (uint64_t)at + j < check_at + 4U) continue;
            sum += word;
            sum = (sum & 0xffffU) + (sum >> 16U);
        }
        at += (int64_t)take;
    }
    sum = (sum & 0xffffU) + (sum >> 16U);
    return sum + (uint32_t)n == declared;
}
#endif
