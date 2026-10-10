/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded UTF-8 validation for the new GLB JSON and Avro string framing.
 */
#ifndef XX_UTF8_VALIDATION_H
#define XX_UTF8_VALIDATION_H
static XXFC_MAYBE_UNUSED bool bounded_utf8(const uint8_t *p, size_t size, xx_pd_struct *pd)
{
    size_t at = 0, tick = 0;
    while (at < size) {
        uint8_t first = p[at++], low = 0x80, high = 0xbf;
        unsigned extra, i;
        if (!(tick++ & 4095) && pd && xx_pd_is_stopped(pd)) return false;
        if (first < 0x80) continue;
        if (first >= 0xc2 && first <= 0xdf) extra = 1;
        else if (first >= 0xe0 && first <= 0xef) {
            extra = 2;
            if (first == 0xe0) low = 0xa0;
            if (first == 0xed) high = 0x9f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            extra = 3;
            if (first == 0xf0) low = 0x90;
            if (first == 0xf4) high = 0x8f;
        } else return false;
        if (extra > size - at || p[at] < low || p[at] > high) return false;
        for (i = 1; i < extra; ++i)
            if (p[at + i] < 0x80 || p[at + i] > 0xbf) return false;
        at += extra;
    }
    return !pd || !xx_pd_is_stopped(pd);
}
#endif
