/* Copyright (c) 2024-2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "xx_codepage_internal.h"
/* Without XXFC_CODEPAGE_ALL, cmake/xxfclib_codepages.cmake compiles only the
 * registry, CP437 and the Unicode encodings, and defines
 * XXFC_CODEPAGE_CP437_ONLY here. A build that bypasses it keeps every page.
 * The East Asian pages are optional on their own: the same file defines
 * XXFC_CODEPAGE_JAPANESE, _CHINESE and _KOREAN for the ones it compiles. */
#ifndef XXFC_CODEPAGE_CP437_ONLY
extern const xx_codepage xx_codepage_ascii;
extern const xx_codepage xx_codepage_iso8859_1;
extern const xx_codepage xx_codepage_iso8859_2;
extern const xx_codepage xx_codepage_iso8859_3;
extern const xx_codepage xx_codepage_iso8859_4;
extern const xx_codepage xx_codepage_iso8859_5;
extern const xx_codepage xx_codepage_iso8859_6;
extern const xx_codepage xx_codepage_iso8859_7;
extern const xx_codepage xx_codepage_iso8859_8;
extern const xx_codepage xx_codepage_iso8859_9;
extern const xx_codepage xx_codepage_iso8859_10;
extern const xx_codepage xx_codepage_iso8859_11;
extern const xx_codepage xx_codepage_iso8859_13;
extern const xx_codepage xx_codepage_iso8859_14;
extern const xx_codepage xx_codepage_iso8859_15;
extern const xx_codepage xx_codepage_iso8859_16;
extern const xx_codepage xx_codepage_cp1250;
extern const xx_codepage xx_codepage_cp1251;
extern const xx_codepage xx_codepage_cp1252;
extern const xx_codepage xx_codepage_cp1253;
extern const xx_codepage xx_codepage_cp1254;
extern const xx_codepage xx_codepage_cp1255;
extern const xx_codepage xx_codepage_cp1256;
extern const xx_codepage xx_codepage_cp1257;
extern const xx_codepage xx_codepage_cp1258;
#endif
extern const xx_codepage xx_codepage_cp437;
#ifndef XXFC_CODEPAGE_CP437_ONLY
extern const xx_codepage xx_codepage_cp850;
extern const xx_codepage xx_codepage_cp852;
extern const xx_codepage xx_codepage_cp855;
extern const xx_codepage xx_codepage_cp857;
extern const xx_codepage xx_codepage_cp858;
extern const xx_codepage xx_codepage_cp860;
extern const xx_codepage xx_codepage_cp861;
extern const xx_codepage xx_codepage_cp862;
extern const xx_codepage xx_codepage_cp863;
extern const xx_codepage xx_codepage_cp864;
extern const xx_codepage xx_codepage_cp865;
extern const xx_codepage xx_codepage_cp866;
extern const xx_codepage xx_codepage_cp869;
extern const xx_codepage xx_codepage_tis_620;
extern const xx_codepage xx_codepage_cp874;
extern const xx_codepage xx_codepage_mac_roman;
extern const xx_codepage xx_codepage_hp_roman8;
extern const xx_codepage xx_codepage_koi8_r;
extern const xx_codepage xx_codepage_koi8_u;
#endif
extern const xx_codepage xx_codepage_utf8;
extern const xx_codepage xx_codepage_utf16le;
extern const xx_codepage xx_codepage_utf16be;
extern const xx_codepage xx_codepage_utf32le;
extern const xx_codepage xx_codepage_utf32be;
#ifndef XXFC_CODEPAGE_CP437_ONLY
extern const xx_codepage xx_codepage_cp037;
#endif
#ifdef XXFC_CODEPAGE_JAPANESE
extern const xx_codepage xx_codepage_cp932;
#endif
#ifdef XXFC_CODEPAGE_CHINESE
extern const xx_codepage xx_codepage_cp936;
extern const xx_codepage xx_codepage_cp950;
#endif
#ifdef XXFC_CODEPAGE_KOREAN
extern const xx_codepage xx_codepage_cp949;
#endif
#ifndef XXFC_CODEPAGE_CP437_ONLY
extern const xx_codepage xx_codepage_petscii_upper;
extern const xx_codepage xx_codepage_petscii_lower;
extern const xx_codepage xx_codepage_riscos;
#endif
static const xx_codepage *const pages[] = {
#ifndef XXFC_CODEPAGE_CP437_ONLY
    &xx_codepage_ascii,          &xx_codepage_iso8859_1,      &xx_codepage_iso8859_2,      &xx_codepage_iso8859_3,      &xx_codepage_iso8859_4,
    &xx_codepage_iso8859_5,      &xx_codepage_iso8859_6,      &xx_codepage_iso8859_7,      &xx_codepage_iso8859_8,      &xx_codepage_iso8859_9,
    &xx_codepage_iso8859_10,     &xx_codepage_iso8859_11,     &xx_codepage_iso8859_13,     &xx_codepage_iso8859_14,     &xx_codepage_iso8859_15,
    &xx_codepage_iso8859_16,     &xx_codepage_cp1250,         &xx_codepage_cp1251,         &xx_codepage_cp1252,         &xx_codepage_cp1253,
    &xx_codepage_cp1254,         &xx_codepage_cp1255,         &xx_codepage_cp1256,         &xx_codepage_cp1257,         &xx_codepage_cp1258,
#endif
    &xx_codepage_cp437,
#ifndef XXFC_CODEPAGE_CP437_ONLY
    &xx_codepage_cp850,          &xx_codepage_cp852,          &xx_codepage_cp855,          &xx_codepage_cp857,          &xx_codepage_cp858,
    &xx_codepage_cp860,          &xx_codepage_cp861,          &xx_codepage_cp862,          &xx_codepage_cp863,          &xx_codepage_cp864,
    &xx_codepage_cp865,          &xx_codepage_cp866,          &xx_codepage_cp869,          &xx_codepage_tis_620,        &xx_codepage_cp874,
    &xx_codepage_mac_roman,      &xx_codepage_hp_roman8,      &xx_codepage_koi8_r,         &xx_codepage_koi8_u,
#endif
    &xx_codepage_utf8,           &xx_codepage_utf16le,        &xx_codepage_utf16be,        &xx_codepage_utf32le,        &xx_codepage_utf32be,
#ifndef XXFC_CODEPAGE_CP437_ONLY
    &xx_codepage_petscii_upper,  &xx_codepage_petscii_lower,  &xx_codepage_riscos,         &xx_codepage_cp037,
#endif
#ifdef XXFC_CODEPAGE_JAPANESE
    &xx_codepage_cp932,
#endif
#ifdef XXFC_CODEPAGE_CHINESE
    &xx_codepage_cp936,
#endif
#ifdef XXFC_CODEPAGE_KOREAN
    &xx_codepage_cp949,
#endif
#ifdef XXFC_CODEPAGE_CHINESE
    &xx_codepage_cp950,
#endif
};

size_t xx_codepage_count(void) { return sizeof(pages) / sizeof(pages[0]); }
const xx_codepage *xx_codepage_at(size_t i) { return i < xx_codepage_count() ? pages[i] : NULL; }
static int next_name(const char **s)
{
    unsigned char c;
    do {
        c = (unsigned char)**s;
        if (c)
            ++*s;
    } while (c == '-' || c == '_' || c == ' ');
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}
const xx_codepage *xx_codepage_find(const char *name)
{
    size_t i;
    if (!name)
        return NULL;
    for (i = 0; i < xx_codepage_count(); ++i) {
        const char *a = name, *b = pages[i]->name;
        int x, y;
        do {
            x = next_name(&a);
            y = next_name(&b);
        } while (x && x == y);
        if (x == y)
            return pages[i];
    }
    return NULL;
}
xx_codepage_status xx_codepage_decode(const xx_codepage *cp, const unsigned char *s, size_t n, size_t *used, uint32_t *out)
{
    if (used)
        *used = 0;
    if (!cp || !cp->decode || !used || !out || (!s && n))
        return XX_CODEPAGE_INVALID;
    if (!n)
        return XX_CODEPAGE_INCOMPLETE;
    return cp->decode(cp, s, n, used, out);
}
xx_codepage_status xx_codepage_encode(const xx_codepage *cp, uint32_t u, unsigned char *s, size_t n, size_t *used)
{
    if (used)
        *used = 0;
    if (!cp || !cp->encode || !used || (!s && n) || u > 0x10ffff || (u >= 0xd800 && u <= 0xdfff))
        return XX_CODEPAGE_INVALID;
    return cp->encode(cp, u, s, n, used);
}
xx_codepage_status xx_codepage_sb_decode(const xx_codepage *cp, const unsigned char *s, size_t n, size_t *used, uint32_t *out)
{
    uint32_t u;
    (void)n;
    u = ((const uint32_t *)cp->context)[s[0]];
    if (u == UINT32_MAX)
        return XX_CODEPAGE_INVALID;
    *out = u;
    *used = 1;
    return XX_CODEPAGE_OK;
}
xx_codepage_status xx_codepage_sb_encode(const xx_codepage *cp, uint32_t u, unsigned char *s, size_t n, size_t *used)
{
    size_t i;
    for (i = 0; i < 256; ++i)
        if (((const uint32_t *)cp->context)[i] == u) {
            if (!n)
                return XX_CODEPAGE_BUFFER_TOO_SMALL;
            s[0] = (unsigned char)i;
            *used = 1;
            return XX_CODEPAGE_OK;
        }
    return XX_CODEPAGE_UNMAPPABLE;
}

const xx_codepage *xx_codepage_by_number(unsigned int number)
{
    switch (number) {
#ifndef XXFC_CODEPAGE_CP437_ONLY
    case 37:
        return &xx_codepage_cp037;
#endif
    case 437:
        return &xx_codepage_cp437;
#ifndef XXFC_CODEPAGE_CP437_ONLY
    case 850:
        return &xx_codepage_cp850;
    case 852:
        return &xx_codepage_cp852;
    case 855:
        return &xx_codepage_cp855;
    case 857:
        return &xx_codepage_cp857;
    case 858:
        return &xx_codepage_cp858;
    case 860:
        return &xx_codepage_cp860;
    case 861:
        return &xx_codepage_cp861;
    case 862:
        return &xx_codepage_cp862;
    case 863:
        return &xx_codepage_cp863;
    case 864:
        return &xx_codepage_cp864;
    case 865:
        return &xx_codepage_cp865;
    case 866:
        return &xx_codepage_cp866;
    case 869:
        return &xx_codepage_cp869;
    case 874:
        return &xx_codepage_cp874;
#endif
#ifdef XXFC_CODEPAGE_JAPANESE
    case 932:
        return &xx_codepage_cp932;
#endif
#ifdef XXFC_CODEPAGE_CHINESE
    case 936:
        return &xx_codepage_cp936;
    case 950:
        return &xx_codepage_cp950;
#endif
#ifdef XXFC_CODEPAGE_KOREAN
    case 949:
        return &xx_codepage_cp949;
#endif
    case 1200:
        return &xx_codepage_utf16le;
    case 1201:
        return &xx_codepage_utf16be;
#ifndef XXFC_CODEPAGE_CP437_ONLY
    case 1250:
        return &xx_codepage_cp1250;
    case 1251:
        return &xx_codepage_cp1251;
    case 1252:
        return &xx_codepage_cp1252;
    case 1253:
        return &xx_codepage_cp1253;
    case 1254:
        return &xx_codepage_cp1254;
    case 1255:
        return &xx_codepage_cp1255;
    case 1256:
        return &xx_codepage_cp1256;
    case 1257:
        return &xx_codepage_cp1257;
    case 1258:
        return &xx_codepage_cp1258;
    case 10000:
        return &xx_codepage_mac_roman;
#endif
    case 12000:
        return &xx_codepage_utf32le;
    case 12001:
        return &xx_codepage_utf32be;
#ifndef XXFC_CODEPAGE_CP437_ONLY
    case 20127:
        return &xx_codepage_ascii;
    case 20866:
        return &xx_codepage_koi8_r;
    case 21866:
        return &xx_codepage_koi8_u;
    case 28591:
        return &xx_codepage_iso8859_1;
    case 28592:
        return &xx_codepage_iso8859_2;
    case 28593:
        return &xx_codepage_iso8859_3;
    case 28594:
        return &xx_codepage_iso8859_4;
    case 28595:
        return &xx_codepage_iso8859_5;
    case 28596:
        return &xx_codepage_iso8859_6;
    case 28597:
        return &xx_codepage_iso8859_7;
    case 28598:
        return &xx_codepage_iso8859_8;
    case 28599:
        return &xx_codepage_iso8859_9;
    case 28603:
        return &xx_codepage_iso8859_13;
    case 28605:
        return &xx_codepage_iso8859_15;
#endif
    case 65001:
        return &xx_codepage_utf8;
    default:
        return NULL;
    }
}

uint32_t xx_codepage_decode_byte(const xx_codepage *cp, unsigned char byte, uint32_t replacement)
{
    uint32_t scalar;
    size_t used;
    return xx_codepage_decode(cp, &byte, 1, &used, &scalar) == XX_CODEPAGE_OK ? scalar : replacement;
}
xx_codepage_status xx_codepage_to_utf8(const xx_codepage *cp, const unsigned char *src, size_t size, unsigned char *out, size_t capacity,
                                       size_t *used, size_t *written)
{
    size_t at = 0, pos = 0;
    if (used)
        *used = 0;
    if (written)
        *written = 0;
    if (!cp || !cp->decode || !used || !written || (!src && size) || (!out && capacity))
        return XX_CODEPAGE_INVALID;
    while (at < size) {
        uint32_t scalar;
        size_t read, wrote;
        xx_codepage_status status = xx_codepage_decode(cp, src + at, size - at, &read, &scalar);
        if (status != XX_CODEPAGE_OK)
            return status;
        if (!read || read > size - at)
            return XX_CODEPAGE_INVALID;
        status = xx_codepage_encode(&xx_codepage_utf8, scalar, out ? out + pos : NULL, capacity - pos, &wrote);
        if (status != XX_CODEPAGE_OK)
            return status;
        at += read;
        pos += wrote;
        *used = at;
        *written = pos;
    }
    return XX_CODEPAGE_OK;
}

size_t xx_codepage_byte_to_utf8(const xx_codepage *cp, unsigned char byte, unsigned char *out, size_t capacity)
{
    size_t used, written;
    return xx_codepage_to_utf8(cp, &byte, 1, out, capacity, &used, &written) == XX_CODEPAGE_OK ? written : 0;
}
