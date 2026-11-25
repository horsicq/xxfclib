/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_SQUASHFS_NOEOS_LZMA_H
#define XX_SQUASHFS_NOEOS_LZMA_H
#include "xxfclib/formats/filesystem/squashfs_sqlz/xx_squashfs_sqlz.h"
#include "xxfclib/data/xx_data.h"

/* Legacy vendor blocks finish with a range-coder flush, without an LZMA
 * end marker. Reuse the existing decoder that recognizes that clean end.
 * Ordinary end-marked streams are tried by the caller before this fallback. */
static bool sqfs_noeos_lzma(const uint8_t *in, size_t n, uint8_t *out, size_t cap, bool exact, size_t *written)
{
    uint32_t dictionary;
    size_t produced = 0;
    if (!in || !out || !written || n <= 5U || !cap || in[0] >= 225U) return false;
    *written = 0;
    dictionary = (uint32_t)xx_data_get_u32(in + 1, 4, 0, false);
    if (dictionary < 4096U || dictionary > UINT32_C(0x40000000)) return false;
    if (in[5] == 0U && xx_squashfs_sqlz_lzma_decode(in + 5, n - 5U, in[0], out, cap, exact, &produced)) {
        *written = produced;
        return true;
    }
    if (n > 13U && in[13] == 0U) {
        uint64_t size = xx_data_get_u64(in + 5, 8, 0, false);
        bool known = size != UINT64_MAX;
        size_t want = known && size <= (uint64_t)cap ? (size_t)size : cap;
        if ((!known || (size && size <= (uint64_t)cap)) && (!exact || !known || size == (uint64_t)cap) &&
            xx_squashfs_sqlz_lzma_decode(in + 13, n - 13U, in[0], out, want, known || exact, &produced)) {
            *written = produced;
            return true;
        }
    }
    return false;
}
#endif
