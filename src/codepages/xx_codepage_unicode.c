/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xx_codepage_internal.h"

static xx_codepage_status utf8_decode(const xx_codepage *cp, const unsigned char *s, size_t n, size_t *used, uint32_t *out)
{
    uint32_t u, minimum;
    size_t length, i;
    (void)cp;
    if (s[0] < 0x80) {
        *used = 1;
        *out = s[0];
        return XX_CODEPAGE_OK;
    }
    if (s[0] >= 0xc2 && s[0] <= 0xdf) {
        length = 2;
        minimum = 0x80;
        u = s[0] & 31;
    } else if (s[0] >= 0xe0 && s[0] <= 0xef) {
        length = 3;
        minimum = 0x800;
        u = s[0] & 15;
    } else if (s[0] >= 0xf0 && s[0] <= 0xf4) {
        length = 4;
        minimum = 0x10000;
        u = s[0] & 7;
    } else return XX_CODEPAGE_INVALID;
    for (i = 1; i < length; ++i) {
        if (i == n) return XX_CODEPAGE_INCOMPLETE;
        if ((s[i] & 0xc0) != 0x80) return XX_CODEPAGE_INVALID;
        if (i == 1 && ((s[0] == 0xe0 && s[i] < 0xa0) || (s[0] == 0xed && s[i] >= 0xa0) || (s[0] == 0xf0 && s[i] < 0x90) || (s[0] == 0xf4 && s[i] >= 0x90)))
            return XX_CODEPAGE_INVALID;
        u = (u << 6) | (s[i] & 63);
    }
    if (u < minimum || u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff)) return XX_CODEPAGE_INVALID;
    *used = length;
    *out = u;
    return XX_CODEPAGE_OK;
}
static xx_codepage_status utf8_encode(const xx_codepage *cp, uint32_t u, unsigned char *s, size_t n, size_t *used)
{
    size_t length = u < 0x80 ? 1 : u < 0x800 ? 2 : u < 0x10000 ? 3 : 4, i;
    (void)cp;
    if (n < length) return XX_CODEPAGE_BUFFER_TOO_SMALL;
    for (i = length; i > 1;) {
        s[--i] = 0x80 | (u & 63);
        u >>= 6;
    }
    s[0] = (unsigned char)(u | (length == 1 ? 0 : length == 2 ? 0xc0 : length == 3 ? 0xe0 : 0xf0));
    *used = length;
    return XX_CODEPAGE_OK;
}
static const unsigned int le16 = 2, be16 = 3, le32 = 4, be32 = 5;
static uint32_t read_unit(const unsigned char *s, size_t length, int be)
{
    uint32_t u = 0;
    size_t i;
    for (i = 0; i < length; ++i) u = (u << 8) | s[be ? i : length - 1 - i];
    return u;
}
static void write_unit(unsigned char *s, size_t length, int be, uint32_t u)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        s[be ? length - 1 - i : i] = (unsigned char)u;
        u >>= 8;
    }
}
static xx_codepage_status unicode_decode(const xx_codepage *cp, const unsigned char *s, size_t n, size_t *used, uint32_t *out)
{
    unsigned int mode = *(const unsigned int *)cp->context;
    size_t length = mode & ~1u;
    uint32_t u, low;
    if (n < length) return XX_CODEPAGE_INCOMPLETE;
    u = read_unit(s, length, mode & 1);
    if (length == 2 && u >= 0xd800 && u <= 0xdbff) {
        if (n < 4) return XX_CODEPAGE_INCOMPLETE;
        low = read_unit(s + 2, 2, mode & 1);
        if (low < 0xdc00 || low > 0xdfff) return XX_CODEPAGE_INVALID;
        u = 0x10000 + ((u - 0xd800) << 10) + low - 0xdc00;
        length = 4;
    }
    if (u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff)) return XX_CODEPAGE_INVALID;
    *out = u;
    *used = length;
    return XX_CODEPAGE_OK;
}
static xx_codepage_status unicode_encode(const xx_codepage *cp, uint32_t u, unsigned char *s, size_t n, size_t *used)
{
    unsigned int mode = *(const unsigned int *)cp->context;
    size_t unit = mode & ~1u, length = unit == 2 && u > 0xffff ? 4 : unit;
    if (n < length) return XX_CODEPAGE_BUFFER_TOO_SMALL;
    if (unit == 2 && length == 4) {
        u -= 0x10000;
        write_unit(s, 2, mode & 1, 0xd800 + (u >> 10));
        write_unit(s + 2, 2, mode & 1, 0xdc00 + (u & 1023));
    } else write_unit(s, unit, mode & 1, u);
    *used = length;
    return XX_CODEPAGE_OK;
}
const xx_codepage xx_codepage_utf8 = {"UTF-8", 4, NULL, utf8_decode, utf8_encode};
const xx_codepage xx_codepage_utf16le = {"UTF-16LE", 4, &le16, unicode_decode, unicode_encode};
const xx_codepage xx_codepage_utf16be = {"UTF-16BE", 4, &be16, unicode_decode, unicode_encode};
const xx_codepage xx_codepage_utf32le = {"UTF-32LE", 4, &le32, unicode_decode, unicode_encode};
const xx_codepage xx_codepage_utf32be = {"UTF-32BE", 4, &be32, unicode_decode, unicode_encode};
