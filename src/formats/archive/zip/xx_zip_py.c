/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* Python-zipfile-exact ZIP reader (see xx_zip_py.h): the archive, the
 * output sink, ZipExtFile with the three decoders that stand in for zlib,
 * libbz2 and liblzma, and lzma.decompress() for XZ data. */

#include <xxfclib/formats/archive/zip/xx_zip_py.h>

#include <xxfclib/algo/crc/xx_crc.h>
#include <xxfclib/algo/deflate/xx_deflate.h>
#include <xxfclib/algo/sha/xx_sha.h>
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/rt/xx_rt.h>

#include <string.h>

#include "../../../algo/bzip2/xx_bzip2_random.h"

#if defined(_WIN64)
#include <windows.h>
#define XX_ZIP_PY_SPACE 1
#endif

#define XX_ZIP_PY_SIZE_END_CENT_DIR 22
#define XX_ZIP_PY_SIZE_END_CENT_DIR64 56
#define XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR 20
#define XX_ZIP_PY_SIZE_CENTRAL_DIR 46
#define XX_ZIP_PY_SIZE_FILE_HEADER 30
#define XX_ZIP_PY_MAX_COMMENT 0xFFFF

#define XX_ZIP_PY_MASK_ENCRYPTED 0x0001
#define XX_ZIP_PY_MASK_COMPRESSED_PATCH 0x0020
#define XX_ZIP_PY_MASK_STRONG_ENCRYPTION 0x0040
#define XX_ZIP_PY_MASK_UTF_FILENAME 0x0800

/* The most output reserved up front for one compressed member. The declared
   size can lie, so it is trusted only this far; a larger member grows from
   here. */
#define XX_ZIP_PY_RESERVE_MAX ((size_t)16 << 20)

/* ------------------------------------------------------------------ texts */

void xx_zip_py_free_text(char *text)
{
    xx_rt_free(text);
}

static char *xx_zip_py_strndup(const char *text, size_t length)
{
    char *copy = (char *)xx_rt_malloc(length + 1);
    if (!copy) return NULL;
    if (length) memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char *xx_zip_py_strdup(const char *text)
{
    return xx_zip_py_strndup(text, strlen(text));
}

/* The concatenation of three texts (NULL when out of memory). */
static char *xx_zip_py_concat(const char *a, const char *b, const char *c)
{
    xx_buf_t out;
    xx_buf_init(&out);
    xx_buf_append_str(&out, a);
    xx_buf_append_str(&out, b);
    xx_buf_append_str(&out, c);
    return xx_buf_detach(&out, NULL);
}

/* CPython's cp437 codec, upper half. The lower half is ASCII. */
static const uint16_t xx_zip_py_cp437_high[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static uint16_t xx_zip_py_rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t xx_zip_py_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t xx_zip_py_rd64(const uint8_t *p)
{
    return (uint64_t)xx_zip_py_rd32(p) | ((uint64_t)xx_zip_py_rd32(p + 4) << 32);
}

/* One UTF-8 code point as Python's strict codec accepts it: the bytes used
 * (>= 1) and the code point, or -1 in *code_point for an invalid sequence
 * (one byte is used then). Surrogates and overlong forms are invalid. */
static size_t xx_zip_py_utf8_decode(const uint8_t *text, size_t length, int32_t *code_point)
{
    uint32_t c = text[0], minimum;
    size_t needed;
    *code_point = -1;
    if (c < 0x80) {
        *code_point = (int32_t)c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        needed = 1;
        c &= 0x1F;
        minimum = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        needed = 2;
        c &= 0x0F;
        minimum = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        needed = 3;
        c &= 0x07;
        minimum = 0x10000;
    } else {
        return 1;
    }
    if (length < needed + 1) return 1;
    for (size_t i = 1; i <= needed; ++i) {
        if ((text[i] & 0xC0) != 0x80) return 1;
        c = (c << 6) | (text[i] & 0x3F);
    }
    if (c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 1;
    *code_point = (int32_t)c;
    return needed + 1;
}

static void xx_zip_py_utf8_encode(xx_buf_t *buffer, uint32_t code_point)
{
    char out[4];
    size_t length;
    if (code_point < 0x80) {
        out[0] = (char)code_point;
        length = 1;
    } else if (code_point < 0x800) {
        out[0] = (char)(0xC0 | (code_point >> 6));
        out[1] = (char)(0x80 | (code_point & 0x3F));
        length = 2;
    } else if (code_point < 0x10000) {
        out[0] = (char)(0xE0 | (code_point >> 12));
        out[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        out[2] = (char)(0x80 | (code_point & 0x3F));
        length = 3;
    } else {
        out[0] = (char)(0xF0 | (code_point >> 18));
        out[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        out[3] = (char)(0x80 | (code_point & 0x3F));
        length = 4;
    }
    xx_buf_append(buffer, out, length);
}

/* Decode a member name: UTF-8 (invalid bytes become U+FFFD each) or cp437.
 * NULL when out of memory. */
static char *xx_zip_py_decode_name(const uint8_t *raw, size_t length, bool utf8, size_t *out_length)
{
    xx_buf_t out;
    xx_buf_init(&out);
    for (size_t i = 0; i < length;) {
        if (utf8) {
            int32_t code_point;
            size_t used = xx_zip_py_utf8_decode(raw + i, length - i, &code_point);
            if (code_point < 0)
                xx_zip_py_utf8_encode(&out, 0xFFFD);
            else
                xx_buf_append(&out, raw + i, used);
            i += used;
        } else {
            uint8_t c = raw[i++];
            if (c < 0x80)
                xx_buf_append_char(&out, (char)c);
            else
                xx_zip_py_utf8_encode(&out, xx_zip_py_cp437_high[c - 0x80]);
        }
    }
    return xx_buf_detach(&out, out_length);
}

/* zipfile._sanitize_filename(): the name up to a NUL; '\\' becomes '/' on
 * Windows (os.sep). */
static char *xx_zip_py_sanitize(const char *name, size_t length)
{
    size_t end = 0;
    while (end < length && name[end]) ++end;
    char *result = xx_zip_py_strndup(name, end);
#ifdef _WIN32
    if (result)
        for (char *p = result; *p; ++p)
            if (*p == '\\') *p = '/';
#endif
    return result;
}

/* The code points from U+0080 up that str.isprintable() rejects in CPython
 * 3.13 (Unicode 15.1.0: categories Cc, Cf, Cs, Co, Cn, Zl, Zp and Zs),
 * as ranges in ascending order. repr() escapes them. */
static const uint32_t xx_zip_py_unprintable[712][2] = {
    {0x80, 0xA0}, {0xAD, 0xAD}, {0x378, 0x379}, {0x380, 0x383}, {0x38B, 0x38B}, {0x38D, 0x38D},
    {0x3A2, 0x3A2}, {0x530, 0x530}, {0x557, 0x558}, {0x58B, 0x58C}, {0x590, 0x590}, {0x5C8, 0x5CF},
    {0x5EB, 0x5EE}, {0x5F5, 0x605}, {0x61C, 0x61C}, {0x6DD, 0x6DD}, {0x70E, 0x70F}, {0x74B, 0x74C},
    {0x7B2, 0x7BF}, {0x7FB, 0x7FC}, {0x82E, 0x82F}, {0x83F, 0x83F}, {0x85C, 0x85D}, {0x85F, 0x85F},
    {0x86B, 0x86F}, {0x88F, 0x897}, {0x8E2, 0x8E2}, {0x984, 0x984}, {0x98D, 0x98E}, {0x991, 0x992},
    {0x9A9, 0x9A9}, {0x9B1, 0x9B1}, {0x9B3, 0x9B5}, {0x9BA, 0x9BB}, {0x9C5, 0x9C6}, {0x9C9, 0x9CA},
    {0x9CF, 0x9D6}, {0x9D8, 0x9DB}, {0x9DE, 0x9DE}, {0x9E4, 0x9E5}, {0x9FF, 0xA00}, {0xA04, 0xA04},
    {0xA0B, 0xA0E}, {0xA11, 0xA12}, {0xA29, 0xA29}, {0xA31, 0xA31}, {0xA34, 0xA34}, {0xA37, 0xA37},
    {0xA3A, 0xA3B}, {0xA3D, 0xA3D}, {0xA43, 0xA46}, {0xA49, 0xA4A}, {0xA4E, 0xA50}, {0xA52, 0xA58},
    {0xA5D, 0xA5D}, {0xA5F, 0xA65}, {0xA77, 0xA80}, {0xA84, 0xA84}, {0xA8E, 0xA8E}, {0xA92, 0xA92},
    {0xAA9, 0xAA9}, {0xAB1, 0xAB1}, {0xAB4, 0xAB4}, {0xABA, 0xABB}, {0xAC6, 0xAC6}, {0xACA, 0xACA},
    {0xACE, 0xACF}, {0xAD1, 0xADF}, {0xAE4, 0xAE5}, {0xAF2, 0xAF8}, {0xB00, 0xB00}, {0xB04, 0xB04},
    {0xB0D, 0xB0E}, {0xB11, 0xB12}, {0xB29, 0xB29}, {0xB31, 0xB31}, {0xB34, 0xB34}, {0xB3A, 0xB3B},
    {0xB45, 0xB46}, {0xB49, 0xB4A}, {0xB4E, 0xB54}, {0xB58, 0xB5B}, {0xB5E, 0xB5E}, {0xB64, 0xB65},
    {0xB78, 0xB81}, {0xB84, 0xB84}, {0xB8B, 0xB8D}, {0xB91, 0xB91}, {0xB96, 0xB98}, {0xB9B, 0xB9B},
    {0xB9D, 0xB9D}, {0xBA0, 0xBA2}, {0xBA5, 0xBA7}, {0xBAB, 0xBAD}, {0xBBA, 0xBBD}, {0xBC3, 0xBC5},
    {0xBC9, 0xBC9}, {0xBCE, 0xBCF}, {0xBD1, 0xBD6}, {0xBD8, 0xBE5}, {0xBFB, 0xBFF}, {0xC0D, 0xC0D},
    {0xC11, 0xC11}, {0xC29, 0xC29}, {0xC3A, 0xC3B}, {0xC45, 0xC45}, {0xC49, 0xC49}, {0xC4E, 0xC54},
    {0xC57, 0xC57}, {0xC5B, 0xC5C}, {0xC5E, 0xC5F}, {0xC64, 0xC65}, {0xC70, 0xC76}, {0xC8D, 0xC8D},
    {0xC91, 0xC91}, {0xCA9, 0xCA9}, {0xCB4, 0xCB4}, {0xCBA, 0xCBB}, {0xCC5, 0xCC5}, {0xCC9, 0xCC9},
    {0xCCE, 0xCD4}, {0xCD7, 0xCDC}, {0xCDF, 0xCDF}, {0xCE4, 0xCE5}, {0xCF0, 0xCF0}, {0xCF4, 0xCFF},
    {0xD0D, 0xD0D}, {0xD11, 0xD11}, {0xD45, 0xD45}, {0xD49, 0xD49}, {0xD50, 0xD53}, {0xD64, 0xD65},
    {0xD80, 0xD80}, {0xD84, 0xD84}, {0xD97, 0xD99}, {0xDB2, 0xDB2}, {0xDBC, 0xDBC}, {0xDBE, 0xDBF},
    {0xDC7, 0xDC9}, {0xDCB, 0xDCE}, {0xDD5, 0xDD5}, {0xDD7, 0xDD7}, {0xDE0, 0xDE5}, {0xDF0, 0xDF1},
    {0xDF5, 0xE00}, {0xE3B, 0xE3E}, {0xE5C, 0xE80}, {0xE83, 0xE83}, {0xE85, 0xE85}, {0xE8B, 0xE8B},
    {0xEA4, 0xEA4}, {0xEA6, 0xEA6}, {0xEBE, 0xEBF}, {0xEC5, 0xEC5}, {0xEC7, 0xEC7}, {0xECF, 0xECF},
    {0xEDA, 0xEDB}, {0xEE0, 0xEFF}, {0xF48, 0xF48}, {0xF6D, 0xF70}, {0xF98, 0xF98}, {0xFBD, 0xFBD},
    {0xFCD, 0xFCD}, {0xFDB, 0xFFF}, {0x10C6, 0x10C6}, {0x10C8, 0x10CC}, {0x10CE, 0x10CF}, {0x1249, 0x1249},
    {0x124E, 0x124F}, {0x1257, 0x1257}, {0x1259, 0x1259}, {0x125E, 0x125F}, {0x1289, 0x1289}, {0x128E, 0x128F},
    {0x12B1, 0x12B1}, {0x12B6, 0x12B7}, {0x12BF, 0x12BF}, {0x12C1, 0x12C1}, {0x12C6, 0x12C7}, {0x12D7, 0x12D7},
    {0x1311, 0x1311}, {0x1316, 0x1317}, {0x135B, 0x135C}, {0x137D, 0x137F}, {0x139A, 0x139F}, {0x13F6, 0x13F7},
    {0x13FE, 0x13FF}, {0x1680, 0x1680}, {0x169D, 0x169F}, {0x16F9, 0x16FF}, {0x1716, 0x171E}, {0x1737, 0x173F},
    {0x1754, 0x175F}, {0x176D, 0x176D}, {0x1771, 0x1771}, {0x1774, 0x177F}, {0x17DE, 0x17DF}, {0x17EA, 0x17EF},
    {0x17FA, 0x17FF}, {0x180E, 0x180E}, {0x181A, 0x181F}, {0x1879, 0x187F}, {0x18AB, 0x18AF}, {0x18F6, 0x18FF},
    {0x191F, 0x191F}, {0x192C, 0x192F}, {0x193C, 0x193F}, {0x1941, 0x1943}, {0x196E, 0x196F}, {0x1975, 0x197F},
    {0x19AC, 0x19AF}, {0x19CA, 0x19CF}, {0x19DB, 0x19DD}, {0x1A1C, 0x1A1D}, {0x1A5F, 0x1A5F}, {0x1A7D, 0x1A7E},
    {0x1A8A, 0x1A8F}, {0x1A9A, 0x1A9F}, {0x1AAE, 0x1AAF}, {0x1ACF, 0x1AFF}, {0x1B4D, 0x1B4F}, {0x1B7F, 0x1B7F},
    {0x1BF4, 0x1BFB}, {0x1C38, 0x1C3A}, {0x1C4A, 0x1C4C}, {0x1C89, 0x1C8F}, {0x1CBB, 0x1CBC}, {0x1CC8, 0x1CCF},
    {0x1CFB, 0x1CFF}, {0x1F16, 0x1F17}, {0x1F1E, 0x1F1F}, {0x1F46, 0x1F47}, {0x1F4E, 0x1F4F}, {0x1F58, 0x1F58},
    {0x1F5A, 0x1F5A}, {0x1F5C, 0x1F5C}, {0x1F5E, 0x1F5E}, {0x1F7E, 0x1F7F}, {0x1FB5, 0x1FB5}, {0x1FC5, 0x1FC5},
    {0x1FD4, 0x1FD5}, {0x1FDC, 0x1FDC}, {0x1FF0, 0x1FF1}, {0x1FF5, 0x1FF5}, {0x1FFF, 0x200F}, {0x2028, 0x202F},
    {0x205F, 0x206F}, {0x2072, 0x2073}, {0x208F, 0x208F}, {0x209D, 0x209F}, {0x20C1, 0x20CF}, {0x20F1, 0x20FF},
    {0x218C, 0x218F}, {0x2427, 0x243F}, {0x244B, 0x245F}, {0x2B74, 0x2B75}, {0x2B96, 0x2B96}, {0x2CF4, 0x2CF8},
    {0x2D26, 0x2D26}, {0x2D28, 0x2D2C}, {0x2D2E, 0x2D2F}, {0x2D68, 0x2D6E}, {0x2D71, 0x2D7E}, {0x2D97, 0x2D9F},
    {0x2DA7, 0x2DA7}, {0x2DAF, 0x2DAF}, {0x2DB7, 0x2DB7}, {0x2DBF, 0x2DBF}, {0x2DC7, 0x2DC7}, {0x2DCF, 0x2DCF},
    {0x2DD7, 0x2DD7}, {0x2DDF, 0x2DDF}, {0x2E5E, 0x2E7F}, {0x2E9A, 0x2E9A}, {0x2EF4, 0x2EFF}, {0x2FD6, 0x2FEF},
    {0x3000, 0x3000}, {0x3040, 0x3040}, {0x3097, 0x3098}, {0x3100, 0x3104}, {0x3130, 0x3130}, {0x318F, 0x318F},
    {0x31E4, 0x31EE}, {0x321F, 0x321F}, {0xA48D, 0xA48F}, {0xA4C7, 0xA4CF}, {0xA62C, 0xA63F}, {0xA6F8, 0xA6FF},
    {0xA7CB, 0xA7CF}, {0xA7D2, 0xA7D2}, {0xA7D4, 0xA7D4}, {0xA7DA, 0xA7F1}, {0xA82D, 0xA82F}, {0xA83A, 0xA83F},
    {0xA878, 0xA87F}, {0xA8C6, 0xA8CD}, {0xA8DA, 0xA8DF}, {0xA954, 0xA95E}, {0xA97D, 0xA97F}, {0xA9CE, 0xA9CE},
    {0xA9DA, 0xA9DD}, {0xA9FF, 0xA9FF}, {0xAA37, 0xAA3F}, {0xAA4E, 0xAA4F}, {0xAA5A, 0xAA5B}, {0xAAC3, 0xAADA},
    {0xAAF7, 0xAB00}, {0xAB07, 0xAB08}, {0xAB0F, 0xAB10}, {0xAB17, 0xAB1F}, {0xAB27, 0xAB27}, {0xAB2F, 0xAB2F},
    {0xAB6C, 0xAB6F}, {0xABEE, 0xABEF}, {0xABFA, 0xABFF}, {0xD7A4, 0xD7AF}, {0xD7C7, 0xD7CA}, {0xD7FC, 0xF8FF},
    {0xFA6E, 0xFA6F}, {0xFADA, 0xFAFF}, {0xFB07, 0xFB12}, {0xFB18, 0xFB1C}, {0xFB37, 0xFB37}, {0xFB3D, 0xFB3D},
    {0xFB3F, 0xFB3F}, {0xFB42, 0xFB42}, {0xFB45, 0xFB45}, {0xFBC3, 0xFBD2}, {0xFD90, 0xFD91}, {0xFDC8, 0xFDCE},
    {0xFDD0, 0xFDEF}, {0xFE1A, 0xFE1F}, {0xFE53, 0xFE53}, {0xFE67, 0xFE67}, {0xFE6C, 0xFE6F}, {0xFE75, 0xFE75},
    {0xFEFD, 0xFF00}, {0xFFBF, 0xFFC1}, {0xFFC8, 0xFFC9}, {0xFFD0, 0xFFD1}, {0xFFD8, 0xFFD9}, {0xFFDD, 0xFFDF},
    {0xFFE7, 0xFFE7}, {0xFFEF, 0xFFFB}, {0xFFFE, 0xFFFF}, {0x1000C, 0x1000C}, {0x10027, 0x10027}, {0x1003B, 0x1003B},
    {0x1003E, 0x1003E}, {0x1004E, 0x1004F}, {0x1005E, 0x1007F}, {0x100FB, 0x100FF}, {0x10103, 0x10106}, {0x10134, 0x10136},
    {0x1018F, 0x1018F}, {0x1019D, 0x1019F}, {0x101A1, 0x101CF}, {0x101FE, 0x1027F}, {0x1029D, 0x1029F}, {0x102D1, 0x102DF},
    {0x102FC, 0x102FF}, {0x10324, 0x1032C}, {0x1034B, 0x1034F}, {0x1037B, 0x1037F}, {0x1039E, 0x1039E}, {0x103C4, 0x103C7},
    {0x103D6, 0x103FF}, {0x1049E, 0x1049F}, {0x104AA, 0x104AF}, {0x104D4, 0x104D7}, {0x104FC, 0x104FF}, {0x10528, 0x1052F},
    {0x10564, 0x1056E}, {0x1057B, 0x1057B}, {0x1058B, 0x1058B}, {0x10593, 0x10593}, {0x10596, 0x10596}, {0x105A2, 0x105A2},
    {0x105B2, 0x105B2}, {0x105BA, 0x105BA}, {0x105BD, 0x105FF}, {0x10737, 0x1073F}, {0x10756, 0x1075F}, {0x10768, 0x1077F},
    {0x10786, 0x10786}, {0x107B1, 0x107B1}, {0x107BB, 0x107FF}, {0x10806, 0x10807}, {0x10809, 0x10809}, {0x10836, 0x10836},
    {0x10839, 0x1083B}, {0x1083D, 0x1083E}, {0x10856, 0x10856}, {0x1089F, 0x108A6}, {0x108B0, 0x108DF}, {0x108F3, 0x108F3},
    {0x108F6, 0x108FA}, {0x1091C, 0x1091E}, {0x1093A, 0x1093E}, {0x10940, 0x1097F}, {0x109B8, 0x109BB}, {0x109D0, 0x109D1},
    {0x10A04, 0x10A04}, {0x10A07, 0x10A0B}, {0x10A14, 0x10A14}, {0x10A18, 0x10A18}, {0x10A36, 0x10A37}, {0x10A3B, 0x10A3E},
    {0x10A49, 0x10A4F}, {0x10A59, 0x10A5F}, {0x10AA0, 0x10ABF}, {0x10AE7, 0x10AEA}, {0x10AF7, 0x10AFF}, {0x10B36, 0x10B38},
    {0x10B56, 0x10B57}, {0x10B73, 0x10B77}, {0x10B92, 0x10B98}, {0x10B9D, 0x10BA8}, {0x10BB0, 0x10BFF}, {0x10C49, 0x10C7F},
    {0x10CB3, 0x10CBF}, {0x10CF3, 0x10CF9}, {0x10D28, 0x10D2F}, {0x10D3A, 0x10E5F}, {0x10E7F, 0x10E7F}, {0x10EAA, 0x10EAA},
    {0x10EAE, 0x10EAF}, {0x10EB2, 0x10EFC}, {0x10F28, 0x10F2F}, {0x10F5A, 0x10F6F}, {0x10F8A, 0x10FAF}, {0x10FCC, 0x10FDF},
    {0x10FF7, 0x10FFF}, {0x1104E, 0x11051}, {0x11076, 0x1107E}, {0x110BD, 0x110BD}, {0x110C3, 0x110CF}, {0x110E9, 0x110EF},
    {0x110FA, 0x110FF}, {0x11135, 0x11135}, {0x11148, 0x1114F}, {0x11177, 0x1117F}, {0x111E0, 0x111E0}, {0x111F5, 0x111FF},
    {0x11212, 0x11212}, {0x11242, 0x1127F}, {0x11287, 0x11287}, {0x11289, 0x11289}, {0x1128E, 0x1128E}, {0x1129E, 0x1129E},
    {0x112AA, 0x112AF}, {0x112EB, 0x112EF}, {0x112FA, 0x112FF}, {0x11304, 0x11304}, {0x1130D, 0x1130E}, {0x11311, 0x11312},
    {0x11329, 0x11329}, {0x11331, 0x11331}, {0x11334, 0x11334}, {0x1133A, 0x1133A}, {0x11345, 0x11346}, {0x11349, 0x1134A},
    {0x1134E, 0x1134F}, {0x11351, 0x11356}, {0x11358, 0x1135C}, {0x11364, 0x11365}, {0x1136D, 0x1136F}, {0x11375, 0x113FF},
    {0x1145C, 0x1145C}, {0x11462, 0x1147F}, {0x114C8, 0x114CF}, {0x114DA, 0x1157F}, {0x115B6, 0x115B7}, {0x115DE, 0x115FF},
    {0x11645, 0x1164F}, {0x1165A, 0x1165F}, {0x1166D, 0x1167F}, {0x116BA, 0x116BF}, {0x116CA, 0x116FF}, {0x1171B, 0x1171C},
    {0x1172C, 0x1172F}, {0x11747, 0x117FF}, {0x1183C, 0x1189F}, {0x118F3, 0x118FE}, {0x11907, 0x11908}, {0x1190A, 0x1190B},
    {0x11914, 0x11914}, {0x11917, 0x11917}, {0x11936, 0x11936}, {0x11939, 0x1193A}, {0x11947, 0x1194F}, {0x1195A, 0x1199F},
    {0x119A8, 0x119A9}, {0x119D8, 0x119D9}, {0x119E5, 0x119FF}, {0x11A48, 0x11A4F}, {0x11AA3, 0x11AAF}, {0x11AF9, 0x11AFF},
    {0x11B0A, 0x11BFF}, {0x11C09, 0x11C09}, {0x11C37, 0x11C37}, {0x11C46, 0x11C4F}, {0x11C6D, 0x11C6F}, {0x11C90, 0x11C91},
    {0x11CA8, 0x11CA8}, {0x11CB7, 0x11CFF}, {0x11D07, 0x11D07}, {0x11D0A, 0x11D0A}, {0x11D37, 0x11D39}, {0x11D3B, 0x11D3B},
    {0x11D3E, 0x11D3E}, {0x11D48, 0x11D4F}, {0x11D5A, 0x11D5F}, {0x11D66, 0x11D66}, {0x11D69, 0x11D69}, {0x11D8F, 0x11D8F},
    {0x11D92, 0x11D92}, {0x11D99, 0x11D9F}, {0x11DAA, 0x11EDF}, {0x11EF9, 0x11EFF}, {0x11F11, 0x11F11}, {0x11F3B, 0x11F3D},
    {0x11F5A, 0x11FAF}, {0x11FB1, 0x11FBF}, {0x11FF2, 0x11FFE}, {0x1239A, 0x123FF}, {0x1246F, 0x1246F}, {0x12475, 0x1247F},
    {0x12544, 0x12F8F}, {0x12FF3, 0x12FFF}, {0x13430, 0x1343F}, {0x13456, 0x143FF}, {0x14647, 0x167FF}, {0x16A39, 0x16A3F},
    {0x16A5F, 0x16A5F}, {0x16A6A, 0x16A6D}, {0x16ABF, 0x16ABF}, {0x16ACA, 0x16ACF}, {0x16AEE, 0x16AEF}, {0x16AF6, 0x16AFF},
    {0x16B46, 0x16B4F}, {0x16B5A, 0x16B5A}, {0x16B62, 0x16B62}, {0x16B78, 0x16B7C}, {0x16B90, 0x16E3F}, {0x16E9B, 0x16EFF},
    {0x16F4B, 0x16F4E}, {0x16F88, 0x16F8E}, {0x16FA0, 0x16FDF}, {0x16FE5, 0x16FEF}, {0x16FF2, 0x16FFF}, {0x187F8, 0x187FF},
    {0x18CD6, 0x18CFF}, {0x18D09, 0x1AFEF}, {0x1AFF4, 0x1AFF4}, {0x1AFFC, 0x1AFFC}, {0x1AFFF, 0x1AFFF}, {0x1B123, 0x1B131},
    {0x1B133, 0x1B14F}, {0x1B153, 0x1B154}, {0x1B156, 0x1B163}, {0x1B168, 0x1B16F}, {0x1B2FC, 0x1BBFF}, {0x1BC6B, 0x1BC6F},
    {0x1BC7D, 0x1BC7F}, {0x1BC89, 0x1BC8F}, {0x1BC9A, 0x1BC9B}, {0x1BCA0, 0x1CEFF}, {0x1CF2E, 0x1CF2F}, {0x1CF47, 0x1CF4F},
    {0x1CFC4, 0x1CFFF}, {0x1D0F6, 0x1D0FF}, {0x1D127, 0x1D128}, {0x1D173, 0x1D17A}, {0x1D1EB, 0x1D1FF}, {0x1D246, 0x1D2BF},
    {0x1D2D4, 0x1D2DF}, {0x1D2F4, 0x1D2FF}, {0x1D357, 0x1D35F}, {0x1D379, 0x1D3FF}, {0x1D455, 0x1D455}, {0x1D49D, 0x1D49D},
    {0x1D4A0, 0x1D4A1}, {0x1D4A3, 0x1D4A4}, {0x1D4A7, 0x1D4A8}, {0x1D4AD, 0x1D4AD}, {0x1D4BA, 0x1D4BA}, {0x1D4BC, 0x1D4BC},
    {0x1D4C4, 0x1D4C4}, {0x1D506, 0x1D506}, {0x1D50B, 0x1D50C}, {0x1D515, 0x1D515}, {0x1D51D, 0x1D51D}, {0x1D53A, 0x1D53A},
    {0x1D53F, 0x1D53F}, {0x1D545, 0x1D545}, {0x1D547, 0x1D549}, {0x1D551, 0x1D551}, {0x1D6A6, 0x1D6A7}, {0x1D7CC, 0x1D7CD},
    {0x1DA8C, 0x1DA9A}, {0x1DAA0, 0x1DAA0}, {0x1DAB0, 0x1DEFF}, {0x1DF1F, 0x1DF24}, {0x1DF2B, 0x1DFFF}, {0x1E007, 0x1E007},
    {0x1E019, 0x1E01A}, {0x1E022, 0x1E022}, {0x1E025, 0x1E025}, {0x1E02B, 0x1E02F}, {0x1E06E, 0x1E08E}, {0x1E090, 0x1E0FF},
    {0x1E12D, 0x1E12F}, {0x1E13E, 0x1E13F}, {0x1E14A, 0x1E14D}, {0x1E150, 0x1E28F}, {0x1E2AF, 0x1E2BF}, {0x1E2FA, 0x1E2FE},
    {0x1E300, 0x1E4CF}, {0x1E4FA, 0x1E7DF}, {0x1E7E7, 0x1E7E7}, {0x1E7EC, 0x1E7EC}, {0x1E7EF, 0x1E7EF}, {0x1E7FF, 0x1E7FF},
    {0x1E8C5, 0x1E8C6}, {0x1E8D7, 0x1E8FF}, {0x1E94C, 0x1E94F}, {0x1E95A, 0x1E95D}, {0x1E960, 0x1EC70}, {0x1ECB5, 0x1ED00},
    {0x1ED3E, 0x1EDFF}, {0x1EE04, 0x1EE04}, {0x1EE20, 0x1EE20}, {0x1EE23, 0x1EE23}, {0x1EE25, 0x1EE26}, {0x1EE28, 0x1EE28},
    {0x1EE33, 0x1EE33}, {0x1EE38, 0x1EE38}, {0x1EE3A, 0x1EE3A}, {0x1EE3C, 0x1EE41}, {0x1EE43, 0x1EE46}, {0x1EE48, 0x1EE48},
    {0x1EE4A, 0x1EE4A}, {0x1EE4C, 0x1EE4C}, {0x1EE50, 0x1EE50}, {0x1EE53, 0x1EE53}, {0x1EE55, 0x1EE56}, {0x1EE58, 0x1EE58},
    {0x1EE5A, 0x1EE5A}, {0x1EE5C, 0x1EE5C}, {0x1EE5E, 0x1EE5E}, {0x1EE60, 0x1EE60}, {0x1EE63, 0x1EE63}, {0x1EE65, 0x1EE66},
    {0x1EE6B, 0x1EE6B}, {0x1EE73, 0x1EE73}, {0x1EE78, 0x1EE78}, {0x1EE7D, 0x1EE7D}, {0x1EE7F, 0x1EE7F}, {0x1EE8A, 0x1EE8A},
    {0x1EE9C, 0x1EEA0}, {0x1EEA4, 0x1EEA4}, {0x1EEAA, 0x1EEAA}, {0x1EEBC, 0x1EEEF}, {0x1EEF2, 0x1EFFF}, {0x1F02C, 0x1F02F},
    {0x1F094, 0x1F09F}, {0x1F0AF, 0x1F0B0}, {0x1F0C0, 0x1F0C0}, {0x1F0D0, 0x1F0D0}, {0x1F0F6, 0x1F0FF}, {0x1F1AE, 0x1F1E5},
    {0x1F203, 0x1F20F}, {0x1F23C, 0x1F23F}, {0x1F249, 0x1F24F}, {0x1F252, 0x1F25F}, {0x1F266, 0x1F2FF}, {0x1F6D8, 0x1F6DB},
    {0x1F6ED, 0x1F6EF}, {0x1F6FD, 0x1F6FF}, {0x1F777, 0x1F77A}, {0x1F7DA, 0x1F7DF}, {0x1F7EC, 0x1F7EF}, {0x1F7F1, 0x1F7FF},
    {0x1F80C, 0x1F80F}, {0x1F848, 0x1F84F}, {0x1F85A, 0x1F85F}, {0x1F888, 0x1F88F}, {0x1F8AE, 0x1F8AF}, {0x1F8B2, 0x1F8FF},
    {0x1FA54, 0x1FA5F}, {0x1FA6E, 0x1FA6F}, {0x1FA7D, 0x1FA7F}, {0x1FA89, 0x1FA8F}, {0x1FABE, 0x1FABE}, {0x1FAC6, 0x1FACD},
    {0x1FADC, 0x1FADF}, {0x1FAE9, 0x1FAEF}, {0x1FAF9, 0x1FAFF}, {0x1FB93, 0x1FB93}, {0x1FBCB, 0x1FBEF}, {0x1FBFA, 0x1FFFF},
    {0x2A6E0, 0x2A6FF}, {0x2B73A, 0x2B73F}, {0x2B81E, 0x2B81F}, {0x2CEA2, 0x2CEAF}, {0x2EBE1, 0x2EBEF}, {0x2EE5E, 0x2F7FF},
    {0x2FA1E, 0x2FFFF}, {0x3134B, 0x3134F}, {0x323B0, 0xE00FF}, {0xE01F0, 0x10FFFF}};

static bool xx_zip_py_is_printable(uint32_t code_point)
{
    size_t low = 0, high = sizeof(xx_zip_py_unprintable) / sizeof(xx_zip_py_unprintable[0]);
    while (low < high) {
        size_t middle = (low + high) / 2;
        if (code_point > xx_zip_py_unprintable[middle][1])
            low = middle + 1;
        else if (code_point < xx_zip_py_unprintable[middle][0])
            high = middle;
        else
            return false;
    }
    return true;
}

static void xx_zip_py_repr_hex(xx_buf_t *out, char kind, uint32_t value, unsigned digits)
{
    static const char hex[] = "0123456789abcdef";
    char text[10] = {'\\', kind};
    for (unsigned i = 0; i < digits; ++i) text[2 + i] = hex[(value >> (4 * (digits - 1 - i))) & 0x0F];
    xx_buf_append(out, text, 2 + digits);
}

/* repr() of a Python str (the text is UTF-8) or, with `bytes`, of a bytes
 * object: single quotes unless the text has one and no double quote; the
 * quote and the backslash escaped; \t, \n, \r; \xNN for other control
 * characters and, in bytes, everything from 0x7F up; in a str \xNN, \uNNNN
 * or \UNNNNNNNN for what is not printable. */
static char *xx_zip_py_repr_of(const char *text, size_t length, bool bytes)
{
    xx_buf_t out;
    char quote = memchr(text, '\'', length) && !memchr(text, '"', length) ? '"' : '\'';
    xx_buf_init(&out);
    if (bytes) xx_buf_append_char(&out, 'b');
    xx_buf_append_char(&out, quote);
    for (size_t i = 0; i < length;) {
        unsigned char c = (unsigned char)text[i];
        if (c == (unsigned char)quote || c == '\\') {
            xx_buf_append_char(&out, '\\');
            xx_buf_append_char(&out, (char)c);
        } else if (c == '\t' || c == '\n' || c == '\r') {
            xx_buf_append_char(&out, '\\');
            xx_buf_append_char(&out, c == '\t' ? 't' : c == '\n' ? 'n' : 'r');
        } else if (c < 0x20 || c == 0x7F || (bytes && c >= 0x80)) {
            xx_zip_py_repr_hex(&out, 'x', c, 2);
        } else if (c < 0x80) {
            xx_buf_append_char(&out, (char)c);
        } else {
            int32_t code_point;
            size_t used = xx_zip_py_utf8_decode((const uint8_t *)text + i, length - i, &code_point);
            if (code_point < 0) code_point = 0xFFFD;
            if (xx_zip_py_is_printable((uint32_t)code_point))
                xx_buf_append(&out, text + i, used);
            else if (code_point <= 0xFF)
                xx_zip_py_repr_hex(&out, 'x', (uint32_t)code_point, 2);
            else if (code_point <= 0xFFFF)
                xx_zip_py_repr_hex(&out, 'u', (uint32_t)code_point, 4);
            else
                xx_zip_py_repr_hex(&out, 'U', (uint32_t)code_point, 8);
            i += used;
            continue;
        }
        ++i;
    }
    xx_buf_append_char(&out, quote);
    return xx_buf_detach(&out, NULL);
}

static char *xx_zip_py_repr(const char *text, size_t length)
{
    return xx_zip_py_repr_of(text, length, false);
}

/* "<before><repr(text)><after>" */
static char *xx_zip_py_message(const char *before, const char *text, size_t length, const char *after)
{
    char *repr = xx_zip_py_repr(text, length), *message;
    if (!repr) return NULL;
    message = xx_zip_py_concat(before, repr, after);
    xx_rt_free(repr);
    return message;
}

/* Store a failure. A message that could not be allocated turns the status
 * into XX_ZIP_PY_NO_MEMORY. */
static xx_zip_py_status xx_zip_py_fail(char **error, xx_zip_py_status status, char *message)
{
    if (!message) status = XX_ZIP_PY_NO_MEMORY;
    if (error)
        *error = message;
    else
        xx_rt_free(message);
    return status;
}

static xx_zip_py_status xx_zip_py_fail_text(char **error, xx_zip_py_status status, const char *text)
{
    return xx_zip_py_fail(error, status, xx_zip_py_strdup(text));
}

/* ------------------------------------------------------------------ sink */

/* A write-only, growable memory device for the decoders. It keeps at most
 * `limit` bytes; once the limit is reached it refuses further output so the
 * decoder stops early (`full`). Memory grows with the output, so a member
 * that declares a huge size cannot force a huge allocation beyond what is
 * reserved up front. */
typedef struct xx_zip_py_sink {
    xx_io_device device;
    uint8_t *data;
    size_t size;
    size_t capacity; /* usable bytes at data (committed ones in a reservation) */
    size_t limit;
    size_t reserved; /* address space reserved at data; 0: heap memory */
    uint32_t crc;    /* CRC-32 of the bytes kept, when `checksum` is set */
    bool checksum;
    bool full;
    bool no_memory;
} xx_zip_py_sink;

/* Committed memory grows in steps of at least this much (and at most 64 MiB
   ahead of the output). Readers use only the `size` bytes written: there is
   no guaranteed slack past them (later writes may fill the committed part). */
#define XX_ZIP_PY_COMMIT_STEP ((size_t)1 << 20)
#define XX_ZIP_PY_COMMIT_AHEAD ((size_t)64 << 20)
#define XX_ZIP_PY_SLACK ((size_t)64 << 10)

#ifdef XX_ZIP_PY_SPACE
/* Make at least `needed` bytes usable in the reservation. */
static bool xx_zip_py_sink_commit(xx_zip_py_sink *sink, size_t needed)
{
    size_t step = sink->capacity < XX_ZIP_PY_COMMIT_STEP ? XX_ZIP_PY_COMMIT_STEP : sink->capacity;
    if (step > XX_ZIP_PY_COMMIT_AHEAD) step = XX_ZIP_PY_COMMIT_AHEAD;
    size_t target = needed + 4096;
    if (target < sink->capacity + step) target = sink->capacity + step;
    target = (target + 0xFFFF) & ~(size_t)0xFFFF;
    if (target > sink->reserved) target = sink->reserved;
    if (!VirtualAlloc(sink->data + sink->capacity, target - sink->capacity, MEM_COMMIT, PAGE_READWRITE)) return false;
    sink->capacity = target;
    return true;
}
#endif

/* Room for `needed` bytes in total. */
static bool xx_zip_py_sink_grow(xx_zip_py_sink *sink, size_t needed)
{
#ifdef XX_ZIP_PY_SPACE
    if (sink->reserved) {
        if (needed <= sink->reserved - 4096) return xx_zip_py_sink_commit(sink, needed);
        /* Beyond the reservation: the heap from here on. */
        size_t capacity = sink->reserved;
        while (capacity < needed) capacity = capacity > SIZE_MAX / 2 ? needed : capacity * 2;
        if (capacity > sink->limit) capacity = sink->limit;
        uint8_t *heap = (uint8_t *)xx_rt_malloc(capacity);
        if (!heap) return false;
        if (sink->size) memcpy(heap, sink->data, sink->size);
        VirtualFree(sink->data, 0, MEM_RELEASE);
        sink->data = heap;
        sink->capacity = capacity;
        sink->reserved = 0;
        return true;
    }
#endif
    size_t capacity = sink->capacity ? sink->capacity : 65536;
    while (capacity < needed) capacity = capacity > SIZE_MAX / 2 ? needed : capacity * 2;
    if (capacity > sink->limit) capacity = sink->limit;
    uint8_t *larger = (uint8_t *)xx_rt_realloc(sink->data, capacity);
    if (!larger) return false;
    sink->data = larger;
    sink->capacity = capacity;
    return true;
}

static ssize_t xx_zip_py_sink_write(xx_io_device *self, const void *buffer, size_t count)
{
    xx_zip_py_sink *sink = (xx_zip_py_sink *)self->priv;
    size_t room = sink->limit - sink->size, take = count < room ? count : room;
    if (take > sink->capacity - sink->size && !xx_zip_py_sink_grow(sink, sink->size + take)) {
        sink->no_memory = true;
        return -1;
    }
    if (take) {
        memcpy(sink->data + sink->size, buffer, take);
        /* The CRC-32 of the kept bytes, while they are still in cache:
           the same value as one pass over all of them afterwards. */
        if (sink->checksum) sink->crc = xx_crc32_calc(sink->crc, buffer, take);
    }
    sink->size += take;
    if (take < count) {
        sink->full = true;
        return -1;
    }
    return (ssize_t)count;
}

static int64_t xx_zip_py_sink_size(xx_io_device *self)
{
    return (int64_t)((xx_zip_py_sink *)self->priv)->size;
}

static int xx_zip_py_sink_seek64(xx_io_device *self, int64_t offset, int whence)
{
    /* Sequential output only: allow "seeks" that do not move. */
    xx_zip_py_sink *sink = (xx_zip_py_sink *)self->priv;
    int64_t target = whence == 0 ? offset : (int64_t)sink->size + offset;
    return target == (int64_t)sink->size ? 0 : -1;
}

static int xx_zip_py_sink_seek(xx_io_device *self, long offset, int whence)
{
    return xx_zip_py_sink_seek64(self, offset, whence);
}

static int xx_zip_py_sink_close(xx_io_device *self)
{
    (void)self;
    return 0;
}

static void xx_zip_py_sink_init(xx_zip_py_sink *sink, size_t limit)
{
    memset(sink, 0, sizeof(*sink));
    sink->limit = limit;
    sink->device.write = xx_zip_py_sink_write;
    sink->device.seek = xx_zip_py_sink_seek;
    sink->device.seek64 = xx_zip_py_sink_seek64;
    sink->device.tell = xx_zip_py_sink_size;
    sink->device.size = xx_zip_py_sink_size;
    sink->device.total_size = xx_zip_py_sink_size;
    sink->device.get_total_size = xx_zip_py_sink_size;
    sink->device.close = xx_zip_py_sink_close;
    sink->device.priv = sink;
}

/* Allocate `capacity` bytes (at most the limit) before decoding, for output
 * whose size is known. A failed allocation is not an error: the sink then
 * grows with the output, as without the reservation. */
static void xx_zip_py_sink_reserve(xx_zip_py_sink *sink, size_t capacity)
{
    if (capacity > sink->limit) capacity = sink->limit;
    if (capacity <= sink->capacity || sink->reserved) return;
    uint8_t *larger = (uint8_t *)xx_rt_realloc(sink->data, capacity);
    if (!larger) return;
    sink->data = larger;
    sink->capacity = capacity;
}

/* Reserve address space for `capacity` bytes (at most the limit) and commit
 * memory only as the output arrives, so large output is never copied while
 * it grows and a size that is never reached costs no memory. Output beyond
 * the reservation moves to the heap and grows as usual. Returns false when
 * this is not available (32-bit or non-Windows builds, or the reservation
 * failed); the sink is then unchanged. Call it before any output. */
static bool xx_zip_py_sink_reserve_space(xx_zip_py_sink *sink, size_t capacity)
{
#ifdef XX_ZIP_PY_SPACE
    if (capacity > sink->limit) capacity = sink->limit;
    if (sink->data || capacity > SIZE_MAX - 2 * XX_ZIP_PY_SLACK) return false;
    size_t reserved = (capacity + XX_ZIP_PY_SLACK + 0xFFFF) & ~(size_t)0xFFFF;
    void *base = VirtualAlloc(NULL, reserved, MEM_RESERVE, PAGE_READWRITE);
    if (!base) return false;
    sink->data = (uint8_t *)base;
    sink->capacity = 0;
    sink->reserved = reserved;
    return true;
#else
    (void)sink;
    (void)capacity;
    return false;
#endif
}

static void xx_zip_py_release(uint8_t *data, bool mapped)
{
#ifdef XX_ZIP_PY_SPACE
    if (mapped) {
        if (data) VirtualFree(data, 0, MEM_RELEASE);
        return;
    }
#else
    (void)mapped;
#endif
    xx_rt_free(data);
}

/* Detach the bytes (never NULL on success; NULL when out of memory). */
static uint8_t *xx_zip_py_sink_detach(xx_zip_py_sink *sink, size_t *size, bool *mapped)
{
    uint8_t *data = sink->data ? sink->data : (uint8_t *)xx_rt_malloc(1);
    *mapped = sink->data && sink->reserved;
    *size = data ? sink->size : 0;
    sink->data = NULL;
    sink->size = sink->capacity = sink->reserved = 0;
    return data;
}

static void xx_zip_py_sink_free(xx_zip_py_sink *sink)
{
    xx_zip_py_release(sink->data, sink->reserved != 0);
    sink->data = NULL;
    sink->size = sink->capacity = sink->reserved = 0;
}

void xx_zip_py_free(uint8_t *data, xx_zip_py_owner owner)
{
    if (owner != XX_ZIP_PY_BORROWED) xx_zip_py_release(data, owner == XX_ZIP_PY_MAPPED);
}

/* --------------------------------------------------------------- archive */

typedef struct xx_zip_py_end_record {
    bool zip64;
    uint64_t disk_number;
    uint64_t disk_start;
    uint64_t entries_this_disk;
    uint64_t entries_total;
    uint64_t size;
    uint64_t offset;
    uint64_t location;
    bool file_seek_fails; /* a file object fails where io.BytesIO seeks to 0 */
} xx_zip_py_end_record;

static void xx_zip_py_parse_end(const uint8_t *p, size_t location, xx_zip_py_end_record *rec)
{
    memset(rec, 0, sizeof(*rec));
    rec->disk_number = xx_zip_py_rd16(p + 4);
    rec->disk_start = xx_zip_py_rd16(p + 6);
    rec->entries_this_disk = xx_zip_py_rd16(p + 8);
    rec->entries_total = xx_zip_py_rd16(p + 10);
    rec->size = xx_zip_py_rd32(p + 12);
    rec->offset = xx_zip_py_rd32(p + 16);
    rec->location = location;
}

/* zipfile._EndRecData64() over io.BytesIO: XX_ZIP_PY_OK or the failure
 * (*error set when error is not NULL). Its two seeks count back from the end
 * record; io.BytesIO.seek() stops at 0 where that is before the start, and
 * the read goes on from there. */
static xx_zip_py_status xx_zip_py_end_rec64(const uint8_t *data, size_t size, xx_zip_py_end_record *rec, char **error)
{
    size_t at = rec->location >= XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR ? (size_t)rec->location - XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR : 0;
    if (size - at < XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR) return XX_ZIP_PY_OK;
    const uint8_t *locator = data + at;
    if (memcmp(locator, "PK\x06\x07", 4) != 0) return XX_ZIP_PY_OK;
    uint32_t disk_number = xx_zip_py_rd32(locator + 4);
    uint32_t disks = xx_zip_py_rd32(locator + 16);
    if (disk_number != 0 || disks > 1) {
        if (!error) return XX_ZIP_PY_BAD;
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "zipfiles that span multiple disks are not supported");
    }
    size_t back = XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR + XX_ZIP_PY_SIZE_END_CENT_DIR64;
    rec->file_seek_fails = rec->location >= XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR && rec->location < back;
    at = rec->location >= back ? (size_t)rec->location - back : 0;
    if (size - at < XX_ZIP_PY_SIZE_END_CENT_DIR64) return XX_ZIP_PY_OK;
    const uint8_t *end64 = data + at;
    if (memcmp(end64, "PK\x06\x06", 4) != 0) return XX_ZIP_PY_OK;
    rec->zip64 = true;
    rec->disk_number = xx_zip_py_rd32(end64 + 16);
    rec->disk_start = xx_zip_py_rd32(end64 + 20);
    rec->entries_this_disk = xx_zip_py_rd64(end64 + 24);
    rec->entries_total = xx_zip_py_rd64(end64 + 32);
    rec->size = xx_zip_py_rd64(end64 + 40);
    rec->offset = xx_zip_py_rd64(end64 + 48);
    return XX_ZIP_PY_OK;
}

/* zipfile._EndRecData(): *found tells whether a record was found; a failure
 * status is BadZipFile (or out of memory for its text). */
static xx_zip_py_status xx_zip_py_end_rec(const uint8_t *data, size_t size, xx_zip_py_end_record *rec, bool *found,
                                          char **error)
{
    *found = false;
    if (size < XX_ZIP_PY_SIZE_END_CENT_DIR) return XX_ZIP_PY_OK;
    const uint8_t *tail = data + size - XX_ZIP_PY_SIZE_END_CENT_DIR;
    if (memcmp(tail, "PK\x05\x06", 4) == 0 && tail[20] == 0 && tail[21] == 0) {
        xx_zip_py_parse_end(tail, size - XX_ZIP_PY_SIZE_END_CENT_DIR, rec);
        *found = true;
        return xx_zip_py_end_rec64(data, size, rec, error);
    }
    size_t start = size > (size_t)XX_ZIP_PY_MAX_COMMENT + XX_ZIP_PY_SIZE_END_CENT_DIR
                       ? size - XX_ZIP_PY_MAX_COMMENT - XX_ZIP_PY_SIZE_END_CENT_DIR
                       : 0;
    /* data.rfind(stringEndArchive) */
    for (size_t at = size - 4 + 1; at-- > start;) {
        if (data[at] != 'P' || memcmp(data + at, "PK\x05\x06", 4) != 0) continue;
        if (size - at < XX_ZIP_PY_SIZE_END_CENT_DIR) return XX_ZIP_PY_OK;
        xx_zip_py_parse_end(data + at, at, rec);
        *found = true;
        return xx_zip_py_end_rec64(data, size, rec, error);
    }
    return XX_ZIP_PY_OK;
}

bool xx_zip_py_is_zipfile(const uint8_t *data, size_t size)
{
    xx_zip_py_end_record rec;
    bool found;
    return xx_zip_py_end_rec(data, size, &rec, &found, NULL) == XX_ZIP_PY_OK && found;
}

/* A UserWarning of zipfile.ZipFile(). False when out of memory. */
static bool xx_zip_py_warn(xx_zip_py *zip, const char *text)
{
    char **warnings = (char **)xx_rt_realloc(zip->warnings, (zip->warning_count + 1) * sizeof(*warnings));
    if (!warnings) return false;
    zip->warnings = warnings;
    warnings[zip->warning_count] = xx_zip_py_strdup(text);
    if (!warnings[zip->warning_count]) return false;
    ++zip->warning_count;
    return true;
}

bool xx_zip_py_file_seek_fails(const uint8_t *data, size_t size)
{
    xx_zip_py_end_record rec;
    bool found;
    return xx_zip_py_end_rec(data, size, &rec, &found, NULL) == XX_ZIP_PY_OK && found && rec.file_seek_fails;
}

/* ZipInfo._decodeExtra(): NULL, or the text of the BadZipFile it raises.
 * CPython also raises for a truncated extra record; parsing just stops
 * here. *no_memory is set when memory ran out. */
static const char *xx_zip_py_decode_extra(xx_zip_py *zip, xx_zip_py_info *info, const uint8_t *extra, size_t length,
                                          uint32_t name_crc, bool *no_memory)
{
    while (length >= 4) {
        uint16_t type = xx_zip_py_rd16(extra);
        size_t record = xx_zip_py_rd16(extra + 2);
        if (record + 4 > length) break;
        const uint8_t *p = extra + 4;
        if (type == 0x0001) {
            /* struct.unpack('<Q', data[:8]) for each field that is all ones */
            size_t left = record;
            if (info->file_size == 0xFFFFFFFFu || info->file_size == UINT64_MAX) {
                if (left < 8) return "Corrupt zip64 extra field. File size not found.";
                info->file_size = xx_zip_py_rd64(p);
                p += 8;
                left -= 8;
            }
            if (info->compress_size == 0xFFFFFFFFu) {
                if (left < 8) return "Corrupt zip64 extra field. Compress size not found.";
                info->compress_size = xx_zip_py_rd64(p);
                p += 8;
                left -= 8;
            }
            if (info->header_offset == 0xFFFFFFFF) {
                if (left < 8) return "Corrupt zip64 extra field. Header offset not found.";
                uint64_t offset = xx_zip_py_rd64(p);
                info->header_offset = offset > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)offset;
            }
        } else if (type == 0x7075) {
            /* Info-ZIP Unicode Path Extra Field: version, CRC-32 of the
               name it replaces, the name in UTF-8 */
            if (record < 5) return "Corrupt unicode path extra field (0x7075)";
            if (p[0] == 1 && xx_zip_py_rd32(p + 1) == name_crc) {
                size_t name_length = record - 5, decoded_length = 0;
                for (size_t i = 0; i < name_length;) {
                    int32_t code_point;
                    i += xx_zip_py_utf8_decode(p + 5 + i, name_length - i, &code_point);
                    if (code_point < 0) return "Corrupt unicode path extra field (0x7075): invalid utf-8 bytes";
                }
                if (!name_length) {
                    if (!xx_zip_py_warn(zip, "Empty unicode path extra field (0x7075)")) *no_memory = true;
                } else {
                    char *decoded = xx_zip_py_decode_name(p + 5, name_length, true, &decoded_length);
                    char *sanitized = decoded ? xx_zip_py_sanitize(decoded, decoded_length) : NULL;
                    xx_rt_free(decoded);
                    if (!sanitized) {
                        *no_memory = true;
                    } else {
                        xx_rt_free(info->filename);
                        info->filename = sanitized;
                    }
                }
                if (*no_memory) return NULL;
            }
        }
        extra += record + 4;
        length -= record + 4;
    }
    return NULL;
}

typedef struct xx_zip_py_order {
    int64_t offset;
    size_t index;
} xx_zip_py_order;

static int xx_zip_py_order_compare(const void *a, const void *b)
{
    const xx_zip_py_order *left = (const xx_zip_py_order *)a, *right = (const xx_zip_py_order *)b;
    if (left->offset != right->offset) return left->offset < right->offset ? -1 : 1;
    return left->index < right->index ? -1 : (left->index > right->index);
}

static xx_zip_py_status xx_zip_py_open_fail(xx_zip_py *zip, char **error, xx_zip_py_status status, const char *text)
{
    xx_zip_py_close(zip);
    return xx_zip_py_fail_text(error, status, text);
}

xx_zip_py_status xx_zip_py_open(xx_zip_py *zip, const uint8_t *data, size_t size, char **error)
{
    xx_zip_py_end_record rec;
    bool found = false;
    memset(zip, 0, sizeof(*zip));
    zip->data = data;
    zip->size = size;
    if (error) *error = NULL;

    xx_zip_py_status status = xx_zip_py_end_rec(data, size, &rec, &found, error);
    if (status != XX_ZIP_PY_OK) return status;
    if (!found) return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "File is not a zip file");

    /* concat = location - size_cd - offset_cd [- zip64 records];
       start_dir = offset_cd + concat */
    uint64_t trailer = rec.zip64 ? XX_ZIP_PY_SIZE_END_CENT_DIR64 + XX_ZIP_PY_SIZE_END_CENT_DIR64_LOCATOR : 0;
    if (rec.size > rec.location || trailer > rec.location - rec.size)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "Bad offset for central directory");
    uint64_t start_dir = rec.location - rec.size - trailer;
    int64_t concat = (int64_t)start_dir - (rec.offset > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)rec.offset);

    const uint8_t *cd = data + start_dir;
    size_t cd_size = (size_t)(rec.size < size - start_dir ? rec.size : size - start_dir);
    size_t capacity = 0, position = 0;
    uint64_t total = 0;
    while (total < rec.size) {
        if (cd_size - position < XX_ZIP_PY_SIZE_CENTRAL_DIR)
            return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_BAD, "Truncated central directory");
        const uint8_t *entry = cd + position;
        if (memcmp(entry, "PK\x01\x02", 4) != 0)
            return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_BAD, "Bad magic number for central directory");
        size_t name_length = xx_zip_py_rd16(entry + 28), extra_length = xx_zip_py_rd16(entry + 30),
               comment_length = xx_zip_py_rd16(entry + 32);
        position += XX_ZIP_PY_SIZE_CENTRAL_DIR;
        /* io.BytesIO.read() returns short data at the end of the buffer. */
        size_t name_read = name_length < cd_size - position ? name_length : cd_size - position;
        const uint8_t *raw_name = cd + position;
        position += name_read;
        size_t extra_read = extra_length < cd_size - position ? extra_length : cd_size - position;
        const uint8_t *extra = cd + position;
        position += extra_read;
        position += comment_length < cd_size - position ? comment_length : cd_size - position;

        if (zip->count == capacity) {
            size_t larger = capacity ? capacity * 2 : 16;
            xx_zip_py_info *infos = (xx_zip_py_info *)xx_rt_realloc(zip->infos, larger * sizeof(*zip->infos));
            if (!infos) return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_NO_MEMORY, "out of memory");
            zip->infos = infos;
            capacity = larger;
        }
        xx_zip_py_info *info = &zip->infos[zip->count++];
        memset(info, 0, sizeof(*info));
        info->flag_bits = xx_zip_py_rd16(entry + 8);
        info->orig_filename = xx_zip_py_decode_name(raw_name, name_read, (info->flag_bits & XX_ZIP_PY_MASK_UTF_FILENAME) != 0,
                                                    &info->orig_filename_length);
        if (info->orig_filename) info->filename = xx_zip_py_sanitize(info->orig_filename, info->orig_filename_length);
        if (!info->filename) return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_NO_MEMORY, "out of memory");
        info->extract_version = entry[6];
        info->compress_type = xx_zip_py_rd16(entry + 10);
        info->crc = xx_zip_py_rd32(entry + 16);
        info->compress_size = xx_zip_py_rd32(entry + 20);
        info->file_size = xx_zip_py_rd32(entry + 24);
        info->header_offset = xx_zip_py_rd32(entry + 42);
        /* CPython rejects extract_version > 63 with NotImplementedError for
           the whole archive; the field is ignored here. */
        bool no_memory = false;
        const char *corrupt = xx_zip_py_decode_extra(zip, info, extra, extra_read, xx_crc32_calc(0, raw_name, name_read), &no_memory);
        if (no_memory) return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_NO_MEMORY, "out of memory");
        if (corrupt) return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_BAD, corrupt);
        info->header_offset += concat;
        total += XX_ZIP_PY_SIZE_CENTRAL_DIR + name_length + extra_length + comment_length;
    }

    /* end_offset: the start of the next local header in offset order. */
    if (zip->count) {
        xx_zip_py_order *order = (xx_zip_py_order *)xx_rt_malloc(zip->count * sizeof(*order));
        if (!order) return xx_zip_py_open_fail(zip, error, XX_ZIP_PY_NO_MEMORY, "out of memory");
        for (size_t i = 0; i < zip->count; ++i) {
            order[i].offset = zip->infos[i].header_offset;
            order[i].index = i;
        }
        /* A total order: any sort gives the same result. */
        xx_rt_qsort(order, zip->count, sizeof(*order), xx_zip_py_order_compare);
        int64_t end_offset = (int64_t)start_dir;
        for (size_t i = zip->count; i-- > 0;) {
            zip->infos[order[i].index].end_offset = end_offset;
            end_offset = order[i].offset;
        }
        xx_rt_free(order);
    }
    return XX_ZIP_PY_OK;
}

void xx_zip_py_close(xx_zip_py *zip)
{
    for (size_t i = 0; i < zip->count; ++i) {
        xx_rt_free(zip->infos[i].filename);
        xx_rt_free(zip->infos[i].orig_filename);
    }
    xx_rt_free(zip->infos);
    zip->infos = NULL;
    zip->count = 0;
    for (size_t i = 0; i < zip->warning_count; ++i) xx_rt_free(zip->warnings[i]);
    xx_rt_free(zip->warnings);
    zip->warnings = NULL;
    zip->warning_count = 0;
}

bool xx_zip_py_info_is_dir(const xx_zip_py_info *info)
{
    size_t length = strlen(info->filename);
    if (!length) return false;
#ifdef _WIN32
    return info->filename[length - 1] == '/' || info->filename[length - 1] == '\\';
#else
    return info->filename[length - 1] == '/';
#endif
}

xx_zip_py_status xx_zip_py_open_entry(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t *data_offset,
                                      char **warning, char **error)
{
    if (warning) *warning = NULL;
    if (error) *error = NULL;
    if (info->header_offset < 0 || (uint64_t)info->header_offset > zip->size ||
        zip->size - (size_t)info->header_offset < XX_ZIP_PY_SIZE_FILE_HEADER) {
        if (info->header_offset < 0) {
            /* io.BytesIO.seek(): ValueError("negative seek value -N") */
            char digits[24];
            size_t at = sizeof(digits);
            uint64_t value = 0 - (uint64_t)info->header_offset;
            digits[--at] = '\0';
            do digits[--at] = (char)('0' + value % 10);
            while (value /= 10);
            return xx_zip_py_fail(error, XX_ZIP_PY_VALUE_ERROR, xx_zip_py_concat("negative seek value -", digits + at, ""));
        }
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "Truncated file header");
    }
    const uint8_t *header = zip->data + info->header_offset;
    if (memcmp(header, "PK\x03\x04", 4) != 0)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_BAD, "Bad magic number for file header");
    size_t name_length = xx_zip_py_rd16(header + 26), extra_length = xx_zip_py_rd16(header + 28);
    size_t available = zip->size - (size_t)info->header_offset - XX_ZIP_PY_SIZE_FILE_HEADER;
    size_t name_read = name_length < available ? name_length : available;
    uint64_t position = (uint64_t)info->header_offset + XX_ZIP_PY_SIZE_FILE_HEADER + name_read + extra_length;

    if (info->flag_bits & XX_ZIP_PY_MASK_COMPRESSED_PATCH)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_NOT_IMPLEMENTED, "compressed patched data (flag bit 5)");
    if (info->flag_bits & XX_ZIP_PY_MASK_STRONG_ENCRYPTION)
        return xx_zip_py_fail_text(error, XX_ZIP_PY_NOT_IMPLEMENTED, "strong encryption (flag bit 6)");

    size_t local_length = 0;
    char *local_name = xx_zip_py_decode_name(header + XX_ZIP_PY_SIZE_FILE_HEADER, name_read,
                                             (xx_zip_py_rd16(header + 6) & XX_ZIP_PY_MASK_UTF_FILENAME) != 0, &local_length);
    if (!local_name) return xx_zip_py_fail(error, XX_ZIP_PY_NO_MEMORY, NULL);
    bool same = local_length == info->orig_filename_length && memcmp(local_name, info->orig_filename, local_length) == 0;
    if (!same) {
        char *directory = xx_zip_py_repr(info->orig_filename, info->orig_filename_length);
        char *local = xx_zip_py_repr_of((const char *)header + XX_ZIP_PY_SIZE_FILE_HEADER, name_read, true);
        char *message = NULL;
        if (directory && local) {
            xx_buf_t out;
            xx_buf_init(&out);
            xx_buf_append_str(&out, "File name in directory ");
            xx_buf_append_str(&out, directory);
            xx_buf_append_str(&out, " and header ");
            xx_buf_append_str(&out, local);
            xx_buf_append_str(&out, " differ.");
            message = xx_buf_detach(&out, NULL);
        }
        xx_rt_free(directory);
        xx_rt_free(local);
        xx_rt_free(local_name);
        return xx_zip_py_fail(error, XX_ZIP_PY_BAD, message);
    }
    xx_rt_free(local_name);

    if (position + info->compress_size < position || (int64_t)(position + info->compress_size) > info->end_offset ||
        position + info->compress_size > (uint64_t)INT64_MAX) {
        char *message = xx_zip_py_message("Overlapped entries: ", info->orig_filename, info->orig_filename_length,
                                          " (possible zip bomb)");
        if (info->end_offset != info->header_offset) return xx_zip_py_fail(error, XX_ZIP_PY_OVERLAPPED, message);
        if (!message) return xx_zip_py_fail(error, XX_ZIP_PY_NO_MEMORY, NULL);
        if (warning)
            *warning = message;
        else
            xx_rt_free(message);
    }

    if (info->flag_bits & XX_ZIP_PY_MASK_ENCRYPTED) {
        char *message = xx_zip_py_message("File ", info->filename, strlen(info->filename),
                                          " is encrypted, password required for extraction");
        return xx_zip_py_fail(error, XX_ZIP_PY_ENCRYPTED, message);
    }
    if (info->compress_type != XX_ZIP_PY_STORED && info->compress_type != XX_ZIP_PY_DEFLATED &&
        info->compress_type != XX_ZIP_PY_BZIP2 && info->compress_type != XX_ZIP_PY_LZMA) {
        return xx_zip_py_fail_text(error, XX_ZIP_PY_NOT_IMPLEMENTED, "That compression method is not supported");
    }
    *data_offset = position;
    return XX_ZIP_PY_OK;
}

/* ------------------------------------------------------------ ZipExtFile */

/* ZipExtFile hands its decompressor the member's data in chunks and acts on
 * what comes back from each one: how much a read returns, when the end of
 * the member is seen and the CRC-32 is checked, and which exception is
 * raised first all depend on what zlib, libbz2 and liblzma make of a partial
 * chunk. The three decoders below reproduce exactly that: each takes the
 * input fed so far and decodes as far as its library would, stopping where
 * the library waits for more input (and, for zlib, more room), reports the
 * library's errors at the same point, and continues on the next call. */

#define XX_ZIP_PY_MIN_READ_SIZE 4096
#define XX_ZIP_PY_MAX_N ((uint64_t)1 << 30) /* ZipExtFile.MAX_N is 1 << 31 - 1 */

typedef enum xx_zip_py_step {
    XX_ZIP_PY_STEP_INPUT,  /* decoded as far as the input allows */
    XX_ZIP_PY_STEP_OUTPUT, /* the output limit is reached (zlib) */
    XX_ZIP_PY_STEP_END,    /* the end of the stream */
    XX_ZIP_PY_STEP_ERROR,  /* the library's data error */
    XX_ZIP_PY_STEP_MEMORY
} xx_zip_py_step;

typedef struct xx_zip_py_inflate xx_zip_py_inflate;
typedef struct xx_zip_py_bzip2 xx_zip_py_bzip2;
typedef struct xx_zip_py_lzma xx_zip_py_lzma;

/* A ZipExtFile over a member. */
typedef struct xx_zip_py_ext {
    const xx_zip_py_info *info;
    const uint8_t *input;   /* the member's data */
    size_t available;       /* bytes of the archive from there on */
    size_t fed;             /* bytes read from the file so far */
    size_t consumed;        /* Deflate: bytes zlib has taken (the rest is unconsumed_tail) */
    uint64_t compress_left; /* _compress_left */
    uint64_t left;          /* _left */
    bool eof;               /* _eof */
    uint64_t produced;      /* bytes the running _read1() has decompressed */
    xx_zip_py_sink sink;    /* the bytes _read1() returned: data[:_left] of every call */
    bool no_memory;
    xx_zip_py_inflate *inflate;
    xx_zip_py_bzip2 *bzip2;
    xx_zip_py_lzma *lzma;
} xx_zip_py_ext;

/* Decompressed bytes of the running _read1(): kept while they are within
 * data[:_left], counted in any case. */
static void xx_zip_py_emit(xx_zip_py_ext *file, const uint8_t *bytes, size_t count)
{
    uint64_t room = file->produced < file->left ? file->left - file->produced : 0;
    size_t take = count < room ? count : (size_t)room;
    if (take && !file->no_memory && xx_zip_py_sink_write(&file->sink.device, bytes, take) < 0) file->no_memory = true;
    file->produced += count;
}

/* ---- zlib's inflate (raw Deflate) */

typedef struct xx_zip_py_huffman {
    uint16_t count[16];
    uint16_t symbol[288];
    uint16_t fast[512]; /* (symbol << 4) | length, for codes of up to 9 bits */
    bool empty;         /* no code at all: every lookup is an invalid code */
} xx_zip_py_huffman;

/* zlib's inflate_table(): 0, 1 for an over-subscribed set of lengths, 2 for
 * an incomplete one (the table is usable then: zlib accepts one code of one
 * bit outside the code length code). */
static int xx_zip_py_huffman_build(xx_zip_py_huffman *table, const uint8_t *lengths, unsigned count)
{
    uint16_t offsets[16];
    int left = 1;
    memset(table->count, 0, sizeof(table->count));
    memset(table->fast, 0, sizeof(table->fast));
    for (unsigned i = 0; i < count; ++i) ++table->count[lengths[i]];
    table->count[0] = 0;
    table->empty = true;
    for (unsigned length = 1; length <= 15; ++length)
        if (table->count[length]) table->empty = false;
    if (table->empty) return 0;
    for (unsigned length = 1; length <= 15; ++length) {
        left <<= 1;
        left -= table->count[length];
        if (left < 0) return 1;
    }
    offsets[1] = 0;
    for (unsigned length = 1; length < 15; ++length) offsets[length + 1] = (uint16_t)(offsets[length] + table->count[length]);
    for (unsigned i = 0; i < count; ++i)
        if (lengths[i]) table->symbol[offsets[lengths[i]]++] = (uint16_t)i;
    unsigned code = 0, index = 0;
    for (unsigned length = 1; length <= 9; ++length) {
        for (unsigned i = 0; i < table->count[length]; ++i, ++code, ++index) {
            unsigned reversed = 0;
            for (unsigned bit = 0; bit < length; ++bit)
                if (code & (1u << bit)) reversed |= 1u << (length - 1 - bit);
            for (unsigned fill = reversed; fill < 512; fill += 1u << length)
                table->fast[fill] = (uint16_t)((table->symbol[index] << 4) | length);
        }
        code <<= 1;
    }
    return left > 0 ? 2 : 0;
}

/* Whether zlib accepts the incomplete set: its longest code has one bit. */
static bool xx_zip_py_huffman_single(const xx_zip_py_huffman *table)
{
    for (unsigned length = 2; length <= 15; ++length)
        if (table->count[length]) return false;
    return true;
}

/* One code from `bits` (the stream's next bits, zeros past the `available`
 * ones): 0 with the symbol and its length, 1 when zlib would wait for more
 * input, 2 for an invalid code. */
static int xx_zip_py_huffman_decode(const xx_zip_py_huffman *table, uint32_t bits, uint64_t available, unsigned *symbol,
                                    unsigned *length)
{
    if (!available) return 1;
    if (table->empty) return 2;
    unsigned entry = table->fast[bits & 511];
    if (entry) {
        *length = entry & 15;
        *symbol = entry >> 4;
        return *length > available ? 1 : 0;
    }
    int code = 0, first = 0, index = 0;
    for (unsigned size = 1; size <= 15; ++size) {
        code |= (int)(bits & 1);
        bits >>= 1;
        int count = table->count[size];
        if (code - count < first) {
            *length = size;
            *symbol = table->symbol[index + (code - first)];
            return size > available ? 1 : 0;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return 2;
}

enum { XX_ZIP_PY_INFLATE_HEAD, XX_ZIP_PY_INFLATE_STORED, XX_ZIP_PY_INFLATE_CODES, XX_ZIP_PY_INFLATE_DONE };

struct xx_zip_py_inflate {
    uint64_t position; /* bits consumed */
    uint64_t total;    /* bytes decoded */
    uint64_t flushed;  /* bytes handed on */
    int state;
    bool last;         /* the block is the last one */
    uint32_t stored;   /* bytes left of a stored block */
    uint32_t pending;  /* bytes left of the decoded literal or match */
    uint32_t distance; /* of the pending match; 0: a literal */
    uint8_t literal;
    const char *message;
    xx_zip_py_huffman literals, distances;
    uint8_t window[32768];
};

/* The next bits of the input (least significant first), zeros past its end. */
static uint32_t xx_zip_py_inflate_peek(const uint8_t *in, size_t limit, uint64_t position)
{
    size_t at = (size_t)(position >> 3);
    uint64_t value = 0;
    if (limit - at >= 8) {
        value = xx_zip_py_rd64(in + at);
    } else {
        for (unsigned i = 0; at + i < limit; ++i) value |= (uint64_t)in[at + i] << (8 * i);
    }
    return (uint32_t)(value >> (position & 7));
}

static void xx_zip_py_inflate_flush(xx_zip_py_inflate *z, xx_zip_py_ext *file)
{
    while (z->flushed < z->total) {
        size_t at = (size_t)(z->flushed & 32767);
        uint64_t count = z->total - z->flushed;
        if (count > 32768 - at) count = 32768 - at;
        xx_zip_py_emit(file, z->window + at, (size_t)count);
        z->flushed += count;
    }
}

/* The header of a dynamic block at bit `cursor`: zlib's checks in zlib's
 * order. XX_ZIP_PY_STEP_END stands for success here. */
static xx_zip_py_step xx_zip_py_inflate_dynamic(xx_zip_py_inflate *z, const uint8_t *in, size_t fed, uint64_t cursor,
                                                uint64_t *next)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint64_t end = (uint64_t)fed * 8;
    uint8_t lengths[320];
    xx_zip_py_huffman codes;
    if (end - cursor < 14) return XX_ZIP_PY_STEP_INPUT;
    uint32_t bits = xx_zip_py_inflate_peek(in, fed, cursor);
    unsigned literals = (bits & 31) + 257, distances = ((bits >> 5) & 31) + 1, count = ((bits >> 10) & 15) + 4;
    cursor += 14;
    z->message = "too many length or distance symbols";
    if (literals > 286 || distances > 30) return XX_ZIP_PY_STEP_ERROR;
    if (end - cursor < count * 3) return XX_ZIP_PY_STEP_INPUT;
    memset(lengths, 0, 19);
    for (unsigned i = 0; i < count; ++i, cursor += 3) lengths[order[i]] = (uint8_t)(xx_zip_py_inflate_peek(in, fed, cursor) & 7);
    z->message = "invalid code lengths set";
    if (xx_zip_py_huffman_build(&codes, lengths, 19) != 0) return XX_ZIP_PY_STEP_ERROR;
    unsigned have = 0;
    while (have < literals + distances) {
        uint64_t available = end - cursor;
        unsigned symbol = 0, size = 1, extra, copy;
        uint8_t value = 0;
        bits = xx_zip_py_inflate_peek(in, fed, cursor);
        if (codes.empty) {
            /* zlib takes one bit and the value 0 from its "invalid" entry */
            if (!available) return XX_ZIP_PY_STEP_INPUT;
        } else if (xx_zip_py_huffman_decode(&codes, bits, available, &symbol, &size) != 0) {
            return XX_ZIP_PY_STEP_INPUT;
        }
        if (symbol < 16) {
            cursor += size;
            lengths[have++] = (uint8_t)symbol;
            continue;
        }
        extra = symbol == 16 ? 2 : symbol == 17 ? 3 : 7;
        if (available < size + extra) return XX_ZIP_PY_STEP_INPUT;
        cursor += size;
        bits >>= size;
        z->message = "invalid bit length repeat";
        if (symbol == 16) {
            if (!have) return XX_ZIP_PY_STEP_ERROR;
            value = lengths[have - 1];
            copy = 3 + (bits & 3);
        } else if (symbol == 17) {
            copy = 3 + (bits & 7);
        } else {
            copy = 11 + (bits & 127);
        }
        cursor += extra;
        if (have + copy > literals + distances) return XX_ZIP_PY_STEP_ERROR;
        while (copy--) lengths[have++] = value;
    }
    z->message = "invalid code -- missing end-of-block";
    if (!lengths[256]) return XX_ZIP_PY_STEP_ERROR;
    z->message = "invalid literal/lengths set";
    int built = xx_zip_py_huffman_build(&z->literals, lengths, literals);
    if (built == 1 || (built == 2 && !xx_zip_py_huffman_single(&z->literals))) return XX_ZIP_PY_STEP_ERROR;
    z->message = "invalid distances set";
    built = xx_zip_py_huffman_build(&z->distances, lengths + literals, distances);
    if (built == 1 || (built == 2 && !xx_zip_py_huffman_single(&z->distances))) return XX_ZIP_PY_STEP_ERROR;
    *next = cursor;
    return XX_ZIP_PY_STEP_END;
}

/* inflate() over the input fed so far, with room for output up to `limit`
 * bytes in total. Like zlib it goes on past a full output through whatever
 * needs no room (block headers, empty blocks, the end of the stream) and
 * has taken the bits of the literal or match it then stops at. */
static xx_zip_py_step xx_zip_py_inflate_run(xx_zip_py_inflate *z, xx_zip_py_ext *file, uint64_t limit)
{
    static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t distance_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    const uint8_t *in = file->input;
    size_t fed = file->fed;
    uint64_t end = (uint64_t)fed * 8;
    xx_zip_py_step step;
    for (;;) {
        if (z->total - z->flushed >= 16384) xx_zip_py_inflate_flush(z, file);
        if (z->pending) {
            if (z->total == limit) {
                step = XX_ZIP_PY_STEP_OUTPUT;
                break;
            }
            if (!z->distance) {
                z->window[z->total++ & 32767] = z->literal;
                z->pending = 0;
                continue;
            }
            if (z->distance > z->total) {
                z->message = "invalid distance too far back";
                step = XX_ZIP_PY_STEP_ERROR;
                break;
            }
            uint64_t count = limit - z->total;
            if (count > z->pending) count = z->pending;
            z->pending -= (uint32_t)count;
            for (; count; --count, ++z->total) z->window[z->total & 32767] = z->window[(z->total - z->distance) & 32767];
            continue;
        }
        if (z->state == XX_ZIP_PY_INFLATE_HEAD) {
            if (z->last) {
                z->position = (z->position + 7) & ~(uint64_t)7;
                z->state = XX_ZIP_PY_INFLATE_DONE;
                continue;
            }
            if (end - z->position < 3) {
                step = XX_ZIP_PY_STEP_INPUT;
                break;
            }
            uint32_t bits = xx_zip_py_inflate_peek(in, fed, z->position);
            unsigned type = (bits >> 1) & 3;
            if (type == 0) {
                uint64_t at = (z->position + 3 + 7) & ~(uint64_t)7;
                if (end < at + 32) {
                    step = XX_ZIP_PY_STEP_INPUT;
                    break;
                }
                uint32_t sizes = xx_zip_py_rd32(in + (size_t)(at >> 3));
                if ((sizes & 0xFFFF) != ((sizes >> 16) ^ 0xFFFF)) {
                    z->message = "invalid stored block lengths";
                    step = XX_ZIP_PY_STEP_ERROR;
                    break;
                }
                z->stored = sizes & 0xFFFF;
                z->position = at + 32;
                z->state = XX_ZIP_PY_INFLATE_STORED;
            } else if (type == 1) {
                uint8_t lengths[288 + 32];
                memset(lengths, 8, 144);
                memset(lengths + 144, 9, 112);
                memset(lengths + 256, 7, 24);
                memset(lengths + 280, 8, 8);
                memset(lengths + 288, 5, 32);
                xx_zip_py_huffman_build(&z->literals, lengths, 288);
                xx_zip_py_huffman_build(&z->distances, lengths + 288, 32);
                z->position += 3;
                z->state = XX_ZIP_PY_INFLATE_CODES;
            } else if (type == 2) {
                uint64_t next = 0;
                step = xx_zip_py_inflate_dynamic(z, in, fed, z->position + 3, &next);
                if (step != XX_ZIP_PY_STEP_END) break;
                z->position = next;
                z->state = XX_ZIP_PY_INFLATE_CODES;
            } else {
                z->message = "invalid block type";
                step = XX_ZIP_PY_STEP_ERROR;
                break;
            }
            z->last = (bits & 1) != 0;
        } else if (z->state == XX_ZIP_PY_INFLATE_STORED) {
            if (!z->stored) {
                z->state = XX_ZIP_PY_INFLATE_HEAD;
                continue;
            }
            if (z->total == limit) {
                step = XX_ZIP_PY_STEP_OUTPUT;
                break;
            }
            size_t at = (size_t)(z->position >> 3);
            uint64_t count = fed - at;
            if (!count) {
                step = XX_ZIP_PY_STEP_INPUT;
                break;
            }
            if (count > z->stored) count = z->stored;
            if (count > limit - z->total) count = limit - z->total;
            if (count > 8192) count = 8192;
            for (uint64_t i = 0; i < count; ++i) z->window[(z->total + i) & 32767] = in[at + i];
            z->total += count;
            z->stored -= (uint32_t)count;
            z->position += count * 8;
        } else if (z->state == XX_ZIP_PY_INFLATE_CODES) {
            unsigned symbol = 0, size = 0, extra;
            uint64_t cursor = z->position;
            int found = xx_zip_py_huffman_decode(&z->literals, xx_zip_py_inflate_peek(in, fed, cursor), end - cursor, &symbol, &size);
            if (found == 1) {
                step = XX_ZIP_PY_STEP_INPUT;
                break;
            }
            if (found == 2 || symbol > 285) {
                z->message = "invalid literal/length code";
                step = XX_ZIP_PY_STEP_ERROR;
                break;
            }
            cursor += size;
            if (symbol < 256) {
                z->position = cursor;
                z->literal = (uint8_t)symbol;
                z->distance = 0;
                z->pending = 1;
                continue;
            }
            if (symbol == 256) {
                z->position = cursor;
                z->state = XX_ZIP_PY_INFLATE_HEAD;
                continue;
            }
            symbol -= 257;
            extra = length_extra[symbol];
            if (end - cursor < extra) {
                step = XX_ZIP_PY_STEP_INPUT;
                break;
            }
            uint32_t length = length_base[symbol] + (xx_zip_py_inflate_peek(in, fed, cursor) & ((1u << extra) - 1));
            cursor += extra;
            found = xx_zip_py_huffman_decode(&z->distances, xx_zip_py_inflate_peek(in, fed, cursor), end - cursor, &symbol, &size);
            if (found == 1) {
                step = XX_ZIP_PY_STEP_INPUT;
                break;
            }
            if (found == 2 || symbol > 29) {
                z->message = "invalid distance code";
                step = XX_ZIP_PY_STEP_ERROR;
                break;
            }
            cursor += size;
            extra = distance_extra[symbol];
            if (end - cursor < extra) {
                step = XX_ZIP_PY_STEP_INPUT;
                break;
            }
            z->distance = distance_base[symbol] + (xx_zip_py_inflate_peek(in, fed, cursor) & ((1u << extra) - 1));
            z->pending = length;
            z->position = cursor + extra;
        } else {
            step = XX_ZIP_PY_STEP_END;
            break;
        }
    }
    xx_zip_py_inflate_flush(z, file);
    return step;
}

/* ---- libbz2's BZ2_bzDecompress */

enum {
    XX_ZIP_PY_BZ_MAGIC,
    XX_ZIP_PY_BZ_BLOCK_MAGIC,
    XX_ZIP_PY_BZ_BLOCK_HEAD,
    XX_ZIP_PY_BZ_MAP,
    XX_ZIP_PY_BZ_GROUPS,
    XX_ZIP_PY_BZ_SELECTOR_COUNT,
    XX_ZIP_PY_BZ_SELECTORS,
    XX_ZIP_PY_BZ_CODE_START,
    XX_ZIP_PY_BZ_CODE,
    XX_ZIP_PY_BZ_SYMBOL,
    XX_ZIP_PY_BZ_OUTPUT,
    XX_ZIP_PY_BZ_STREAM_CRC,
    XX_ZIP_PY_BZ_DONE
};

#define XX_ZIP_PY_BZ_MAX_SELECTORS 18002

struct xx_zip_py_bzip2 {
    uint64_t position; /* bits consumed */
    int phase;
    unsigned index, table;
    bool last; /* the magic being read is the end of the stream's */
    unsigned level;
    uint32_t stored_crc, combined_crc, origin;
    bool randomised;
    unsigned in_use, alphabet, groups, selector_count;
    int current; /* the code length being read */
    unsigned group_number, group_position;
    bool group_started;
    bool in_run;
    int32_t run_length, run_weight;
    uint32_t block_size, block_max;
    uint32_t *tt;
    /* the block being written out */
    uint32_t out_position, out_used, out_crc;
    unsigned out_k0, out_run, out_byte, random_left, random_at;
    int minimum[6];
    int32_t limit[6][23], base[6][23];
    uint16_t perm[6][258];
    uint8_t lengths[6][258];
    uint8_t mtf[256], seq_to_unseq[256];
    uint8_t selector[XX_ZIP_PY_BZ_MAX_SELECTORS];
    uint32_t crc_table[256];
};

/* The next `count` (at most 24) bits, most significant first, zeros past
 * the end of the input. */
static uint32_t xx_zip_py_bzip2_peek(const uint8_t *in, size_t limit, uint64_t position, unsigned count)
{
    size_t at = (size_t)(position >> 3);
    uint64_t value = 0;
    for (unsigned i = 0; i < 5; ++i) value = (value << 8) | (at + i < limit ? in[at + i] : 0);
    return (uint32_t)((value >> (40 - (unsigned)(position & 7) - count)) & (((uint32_t)1 << count) - 1));
}

/* BZ2_hbCreateDecodeTables() */
static void xx_zip_py_bzip2_tables(int32_t *limit, int32_t *base, uint16_t *perm, const uint8_t *length, int minimum,
                                   int maximum, unsigned alphabet)
{
    int pp = 0, vec = 0;
    for (int i = minimum; i <= maximum; ++i)
        for (unsigned j = 0; j < alphabet; ++j)
            if (length[j] == i) perm[pp++] = (uint16_t)j;
    for (int i = 0; i < 23; ++i) base[i] = 0;
    for (unsigned i = 0; i < alphabet; ++i) ++base[length[i] + 1];
    for (int i = 1; i < 23; ++i) base[i] += base[i - 1];
    for (int i = 0; i < 23; ++i) limit[i] = 0;
    for (int i = minimum; i <= maximum; ++i) {
        vec += base[i + 1] - base[i];
        limit[i] = vec - 1;
        vec <<= 1;
    }
    for (int i = minimum + 1; i <= maximum; ++i) base[i] = ((limit[i - 1] + 1) << 1) - base[i];
}

/* BZ_GET_FAST, with BZ_RAND_UPD_MASK for a randomised block */
static unsigned xx_zip_py_bzip2_get(xx_zip_py_bzip2 *bz)
{
    bz->out_position = bz->tt[bz->out_position];
    unsigned byte = bz->out_position & 0xFF;
    bz->out_position >>= 8;
    ++bz->out_used;
    if (bz->randomised) {
        if (!bz->random_left) {
            bz->random_left = bz2_random_numbers[bz->random_at];
            if (++bz->random_at == 512) bz->random_at = 0;
        }
        if (--bz->random_left == 1) byte ^= 1;
    }
    return byte;
}

/* The block is read: undo the transform. False for libbz2's BZ_DATA_ERROR. */
static bool xx_zip_py_bzip2_block(xx_zip_py_bzip2 *bz)
{
    uint32_t counts[257], block = bz->block_size, *tt = bz->tt;
    if (bz->origin >= block) return false;
    memset(counts, 0, sizeof(counts));
    for (uint32_t i = 0; i < block; ++i) ++counts[(tt[i] & 0xFF) + 1];
    for (unsigned i = 1; i <= 256; ++i) counts[i] += counts[i - 1];
    for (uint32_t i = 0; i < block; ++i) {
        unsigned byte = tt[i] & 0xFF;
        tt[counts[byte]++] |= i << 8;
    }
    bz->out_position = tt[bz->origin] >> 8;
    bz->out_used = 0;
    bz->out_crc = 0xFFFFFFFFu;
    bz->out_run = 0;
    bz->random_left = 0;
    bz->random_at = 0;
    bz->out_k0 = xx_zip_py_bzip2_get(bz);
    return true;
}

/* Write out at most `room` bytes of the block (*written of them): undo the
 * run-length coding, and check the block's CRC at its end. 0: the room is
 * used up, 1: the block is done, 2: libbz2's BZ_DATA_ERROR. */
static int xx_zip_py_bzip2_output(xx_zip_py_bzip2 *bz, xx_zip_py_ext *file, uint64_t room, uint64_t *written)
{
    uint32_t block = bz->block_size, crc = bz->out_crc;
    uint8_t out[4096];
    size_t filled = 0;
    int result;
    *written = 0;
    for (;;) {
        for (; bz->out_run && room; --bz->out_run, --room) {
            out[filled++] = (uint8_t)bz->out_byte;
            crc = (crc << 8) ^ bz->crc_table[(crc >> 24) ^ bz->out_byte];
            if (filled == sizeof(out)) {
                xx_zip_py_emit(file, out, filled);
                *written += filled;
                filled = 0;
            }
        }
        result = 0;
        if (bz->out_run) break;
        result = bz->out_used == block + 1 ? 1 : 2;
        if (bz->out_used >= block + 1) break;
        unsigned k1;
        bz->out_run = 1;
        bz->out_byte = bz->out_k0;
        k1 = xx_zip_py_bzip2_get(bz);
        if (bz->out_used == block + 1) continue;
        if (k1 != bz->out_k0) {
            bz->out_k0 = k1;
            continue;
        }
        bz->out_run = 2;
        k1 = xx_zip_py_bzip2_get(bz);
        if (bz->out_used == block + 1) continue;
        if (k1 != bz->out_k0) {
            bz->out_k0 = k1;
            continue;
        }
        bz->out_run = 3;
        k1 = xx_zip_py_bzip2_get(bz);
        if (bz->out_used == block + 1) continue;
        if (k1 != bz->out_k0) {
            bz->out_k0 = k1;
            continue;
        }
        k1 = xx_zip_py_bzip2_get(bz);
        bz->out_run = k1 + 4;
        bz->out_k0 = xx_zip_py_bzip2_get(bz);
    }
    if (filled) xx_zip_py_emit(file, out, filled);
    *written += filled;
    bz->out_crc = crc;
    if (result == 1) {
        crc = ~crc;
        if (crc != bz->stored_crc) return 2;
        bz->combined_crc = ((bz->combined_crc << 1) | (bz->combined_crc >> 31)) ^ crc;
    }
    return result;
}

/* The sizes of the blocks CPython's output buffer grows by. */
static uint64_t xx_zip_py_buffer_block(unsigned index)
{
    static const uint16_t kilobytes_32[17] = {1, 2, 8, 32, 128, 256, 512, 512, 1024, 1024, 1024, 1024, 2048, 2048, 4096, 4096, 8192};
    return (uint64_t)kilobytes_32[index < 17 ? index : 16] * 32768;
}

/* BZ2Decompressor.decompress() of the input fed so far: a block's bytes
 * appear when its last bit is there, and every check is made where libbz2
 * makes it. As in CPython the call ends when the input is used up, also
 * while libbz2 holds bytes of a block that did not fit the output buffer
 * (32 KiB at first, growing while there is input): they come with the next
 * call, the block's CRC check after them. */
static xx_zip_py_step xx_zip_py_bzip2_run(xx_zip_py_bzip2 *bz, xx_zip_py_ext *file)
{
    static const uint8_t block_magic[6] = {0x31, 0x41, 0x59, 0x26, 0x53, 0x59};
    static const uint8_t end_magic[6] = {0x17, 0x72, 0x45, 0x38, 0x50, 0x90};
    const uint8_t *in = file->input;
    size_t fed = file->fed;
    uint64_t end = (uint64_t)fed * 8, call_output = 0, capacity = xx_zip_py_buffer_block(0);
    unsigned buffer_blocks = 1;
    for (;;) {
        uint64_t available = end - bz->position;
        switch (bz->phase) {
        case XX_ZIP_PY_BZ_OUTPUT: {
            uint64_t written = 0;
            int result = xx_zip_py_bzip2_output(bz, file, capacity - call_output, &written);
            call_output += written;
            if (result == 2) return XX_ZIP_PY_STEP_ERROR;
            if (result == 1) {
                bz->index = 0;
                bz->phase = XX_ZIP_PY_BZ_BLOCK_MAGIC;
            } else if (((bz->position + 7) >> 3) == fed) {
                return XX_ZIP_PY_STEP_INPUT;
            } else {
                capacity += xx_zip_py_buffer_block(buffer_blocks++);
            }
            break;
        }
        case XX_ZIP_PY_BZ_MAGIC: {
            if (available < 8) return XX_ZIP_PY_STEP_INPUT;
            unsigned byte = xx_zip_py_bzip2_peek(in, fed, bz->position, 8);
            if (bz->index < 3 ? byte != (unsigned)"BZh"[bz->index] : (byte < '1' || byte > '9')) return XX_ZIP_PY_STEP_ERROR;
            bz->position += 8;
            if (bz->index++ == 3) {
                bz->level = byte - '0';
                bz->block_max = 100000 * bz->level;
                bz->tt = (uint32_t *)xx_rt_malloc((size_t)bz->block_max * sizeof(uint32_t));
                if (!bz->tt) return XX_ZIP_PY_STEP_MEMORY;
                bz->index = 0;
                bz->phase = XX_ZIP_PY_BZ_BLOCK_MAGIC;
            }
            break;
        }
        case XX_ZIP_PY_BZ_BLOCK_MAGIC: {
            if (available < 8) return XX_ZIP_PY_STEP_INPUT;
            unsigned byte = xx_zip_py_bzip2_peek(in, fed, bz->position, 8);
            if (bz->index == 0) {
                if (byte != block_magic[0] && byte != end_magic[0]) return XX_ZIP_PY_STEP_ERROR;
                bz->last = byte == end_magic[0];
            } else if (byte != (bz->last ? end_magic : block_magic)[bz->index]) {
                return XX_ZIP_PY_STEP_ERROR;
            }
            bz->position += 8;
            if (++bz->index == 6) {
                bz->index = 0;
                bz->phase = bz->last ? XX_ZIP_PY_BZ_STREAM_CRC : XX_ZIP_PY_BZ_BLOCK_HEAD;
            }
            break;
        }
        case XX_ZIP_PY_BZ_BLOCK_HEAD:
            if (available < 57) return XX_ZIP_PY_STEP_INPUT;
            bz->stored_crc = (xx_zip_py_bzip2_peek(in, fed, bz->position, 16) << 16) |
                             xx_zip_py_bzip2_peek(in, fed, bz->position + 16, 16);
            bz->randomised = xx_zip_py_bzip2_peek(in, fed, bz->position + 32, 1) != 0;
            bz->origin = xx_zip_py_bzip2_peek(in, fed, bz->position + 33, 24);
            bz->position += 57;
            if (bz->origin > 10 + bz->block_max) return XX_ZIP_PY_STEP_ERROR;
            bz->phase = XX_ZIP_PY_BZ_MAP;
            break;
        case XX_ZIP_PY_BZ_MAP: {
            if (available < 16) return XX_ZIP_PY_STEP_INPUT;
            unsigned used = xx_zip_py_bzip2_peek(in, fed, bz->position, 16), rows = 0;
            for (unsigned i = 0; i < 16; ++i) rows += (used >> i) & 1;
            if (available < 16 + 16 * (uint64_t)rows) return XX_ZIP_PY_STEP_INPUT;
            bz->position += 16;
            bz->in_use = 0;
            memset(bz->seq_to_unseq, 0, sizeof(bz->seq_to_unseq));
            for (unsigned i = 0; i < 16; ++i) {
                if (!(used & (0x8000u >> i))) continue;
                unsigned row = xx_zip_py_bzip2_peek(in, fed, bz->position, 16);
                bz->position += 16;
                for (unsigned j = 0; j < 16; ++j)
                    if (row & (0x8000u >> j)) bz->seq_to_unseq[bz->in_use++] = (uint8_t)(i * 16 + j);
            }
            if (!bz->in_use) return XX_ZIP_PY_STEP_ERROR;
            bz->alphabet = bz->in_use + 2;
            bz->phase = XX_ZIP_PY_BZ_GROUPS;
            break;
        }
        case XX_ZIP_PY_BZ_GROUPS:
            if (available < 3) return XX_ZIP_PY_STEP_INPUT;
            bz->groups = xx_zip_py_bzip2_peek(in, fed, bz->position, 3);
            bz->position += 3;
            if (bz->groups < 2 || bz->groups > 6) return XX_ZIP_PY_STEP_ERROR;
            bz->phase = XX_ZIP_PY_BZ_SELECTOR_COUNT;
            break;
        case XX_ZIP_PY_BZ_SELECTOR_COUNT:
            if (available < 15) return XX_ZIP_PY_STEP_INPUT;
            bz->selector_count = xx_zip_py_bzip2_peek(in, fed, bz->position, 15);
            bz->position += 15;
            if (bz->selector_count < 1) return XX_ZIP_PY_STEP_ERROR;
            bz->index = 0;
            bz->phase = XX_ZIP_PY_BZ_SELECTORS;
            break;
        case XX_ZIP_PY_BZ_SELECTORS: {
            unsigned j = 0;
            for (;;) {
                if (available <= j) return XX_ZIP_PY_STEP_INPUT;
                if (!xx_zip_py_bzip2_peek(in, fed, bz->position + j, 1)) break;
                if (++j >= bz->groups) return XX_ZIP_PY_STEP_ERROR;
            }
            bz->position += j + 1;
            if (bz->index < XX_ZIP_PY_BZ_MAX_SELECTORS) bz->selector[bz->index] = (uint8_t)j;
            if (++bz->index == bz->selector_count) {
                uint8_t order[6];
                if (bz->selector_count > XX_ZIP_PY_BZ_MAX_SELECTORS) bz->selector_count = XX_ZIP_PY_BZ_MAX_SELECTORS;
                for (unsigned v = 0; v < bz->groups; ++v) order[v] = (uint8_t)v;
                for (unsigned i = 0; i < bz->selector_count; ++i) {
                    unsigned v = bz->selector[i];
                    uint8_t front = order[v];
                    for (; v; --v) order[v] = order[v - 1];
                    order[0] = front;
                    bz->selector[i] = front;
                }
                bz->table = 0;
                bz->phase = XX_ZIP_PY_BZ_CODE_START;
            }
            break;
        }
        case XX_ZIP_PY_BZ_CODE_START:
            if (available < 5) return XX_ZIP_PY_STEP_INPUT;
            bz->current = (int)xx_zip_py_bzip2_peek(in, fed, bz->position, 5);
            bz->position += 5;
            bz->index = 0;
            bz->phase = XX_ZIP_PY_BZ_CODE;
            break;
        case XX_ZIP_PY_BZ_CODE: {
            int current = bz->current;
            uint64_t used = 0;
            for (;;) {
                if (current < 1 || current > 20) return XX_ZIP_PY_STEP_ERROR;
                if (available <= used) return XX_ZIP_PY_STEP_INPUT;
                if (!xx_zip_py_bzip2_peek(in, fed, bz->position + used++, 1)) break;
                if (available <= used) return XX_ZIP_PY_STEP_INPUT;
                if (xx_zip_py_bzip2_peek(in, fed, bz->position + used++, 1))
                    --current;
                else
                    ++current;
            }
            bz->position += used;
            bz->current = current;
            bz->lengths[bz->table][bz->index] = (uint8_t)current;
            if (++bz->index < bz->alphabet) break;
            if (++bz->table < bz->groups) {
                bz->phase = XX_ZIP_PY_BZ_CODE_START;
                break;
            }
            for (unsigned t = 0; t < bz->groups; ++t) {
                int minimum = 32, maximum = 0;
                for (unsigned i = 0; i < bz->alphabet; ++i) {
                    if (bz->lengths[t][i] > maximum) maximum = bz->lengths[t][i];
                    if (bz->lengths[t][i] < minimum) minimum = bz->lengths[t][i];
                }
                xx_zip_py_bzip2_tables(bz->limit[t], bz->base[t], bz->perm[t], bz->lengths[t], minimum, maximum, bz->alphabet);
                bz->minimum[t] = minimum;
            }
            for (unsigned i = 0; i < 256; ++i) bz->mtf[i] = (uint8_t)i;
            bz->group_started = false;
            bz->group_number = 0;
            bz->group_position = 0;
            bz->in_run = false;
            bz->block_size = 0;
            bz->phase = XX_ZIP_PY_BZ_SYMBOL;
            break;
        }
        case XX_ZIP_PY_BZ_SYMBOL: {
            unsigned group = bz->group_number, left = bz->group_position;
            if (!left) {
                if (bz->group_started) ++group;
                if (group >= bz->selector_count) return XX_ZIP_PY_STEP_ERROR;
                left = 50;
            }
            unsigned t = bz->selector[group];
            int size = bz->minimum[t];
            int32_t code;
            for (;;) {
                /* libbz2 has read the bit before it gives up on the code */
                if (available < (uint64_t)size) return XX_ZIP_PY_STEP_INPUT;
                if (size > 20) return XX_ZIP_PY_STEP_ERROR;
                code = (int32_t)xx_zip_py_bzip2_peek(in, fed, bz->position, (unsigned)size);
                if (code <= bz->limit[t][size]) break;
                ++size;
            }
            code -= bz->base[t][size];
            if (code < 0 || code >= 258) return XX_ZIP_PY_STEP_ERROR;
            unsigned symbol = bz->perm[t][code];
            bz->position += (unsigned)size;
            bz->group_started = true;
            bz->group_number = group;
            bz->group_position = left - 1;

            if (symbol < 2) { /* BZ_RUNA, BZ_RUNB */
                if (!bz->in_run) {
                    bz->in_run = true;
                    bz->run_length = -1;
                    bz->run_weight = 1;
                }
                if (bz->run_weight >= 2 * 1024 * 1024) return XX_ZIP_PY_STEP_ERROR;
                bz->run_length += (int32_t)(symbol + 1) * bz->run_weight;
                bz->run_weight *= 2;
                break;
            }
            if (bz->in_run) {
                uint8_t byte = bz->seq_to_unseq[bz->mtf[0]];
                bz->in_run = false;
                for (int32_t count = bz->run_length + 1; count > 0; --count) {
                    if (bz->block_size >= bz->block_max) return XX_ZIP_PY_STEP_ERROR;
                    bz->tt[bz->block_size++] = byte;
                }
            }
            if (symbol == bz->in_use + 1) { /* the end of the block */
                if (!xx_zip_py_bzip2_block(bz)) return XX_ZIP_PY_STEP_ERROR;
                bz->phase = XX_ZIP_PY_BZ_OUTPUT;
                break;
            }
            if (bz->block_size >= bz->block_max || symbol > 256) return XX_ZIP_PY_STEP_ERROR;
            uint8_t byte = bz->mtf[symbol - 1];
            memmove(bz->mtf + 1, bz->mtf, symbol - 1);
            bz->mtf[0] = byte;
            bz->tt[bz->block_size++] = bz->seq_to_unseq[byte];
            break;
        }
        case XX_ZIP_PY_BZ_STREAM_CRC: {
            if (available < 32) return XX_ZIP_PY_STEP_INPUT;
            uint32_t stored = (xx_zip_py_bzip2_peek(in, fed, bz->position, 16) << 16) |
                              xx_zip_py_bzip2_peek(in, fed, bz->position + 16, 16);
            bz->position += 32;
            if (stored != bz->combined_crc) return XX_ZIP_PY_STEP_ERROR;
            bz->phase = XX_ZIP_PY_BZ_DONE;
            break;
        }
        default:
            return XX_ZIP_PY_STEP_END;
        }
    }
}

static xx_zip_py_bzip2 *xx_zip_py_bzip2_new(void)
{
    xx_zip_py_bzip2 *bz = (xx_zip_py_bzip2 *)xx_rt_malloc(sizeof(*bz));
    if (!bz) return NULL;
    memset(bz, 0, sizeof(*bz));
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i << 24;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
        bz->crc_table[i] = crc;
    }
    return bz;
}

/* ---- liblzma's raw LZMA1 decoder */

typedef struct xx_zip_py_lzma_length {
    uint16_t choice, choice2;
    uint16_t low[16][8], mid[16][8], high[256];
} xx_zip_py_lzma_length;

struct xx_zip_py_lzma {
    bool started; /* zipfile.LZMADecompressor has read its header */
    const char *message;
    uint32_t range, code;
    size_t position; /* input bytes consumed */
    unsigned init_left;
    unsigned lc, lp, pb, state;
    uint32_t reps[4];
    uint64_t total, flushed;
    uint64_t dictionary_size;
    size_t at;       /* total % dictionary_size */
    size_t capacity; /* bytes allocated at dictionary */
    uint8_t *dictionary;
    uint16_t *literal;
    uint16_t is_match[12][16], is_rep[12], is_rep_g0[12], is_rep_g1[12], is_rep_g2[12], is_rep0_long[12][16];
    uint16_t slot[4][64], special[128], align[16];
    xx_zip_py_lzma_length match_length, rep_length;
};

/* The range decoder for one symbol. Like liblzma it takes a byte only when
 * the next bit needs it; `starved` tells that the input ended there. Without
 * `update` the probabilities stay as they are: a dry run. */
typedef struct xx_zip_py_rc {
    uint32_t range, code;
    const uint8_t *in;
    size_t position, limit;
    bool update, starved;
} xx_zip_py_rc;

enum { XX_ZIP_PY_LZMA_LITERAL, XX_ZIP_PY_LZMA_MATCH, XX_ZIP_PY_LZMA_SHORT_REP, XX_ZIP_PY_LZMA_REP, XX_ZIP_PY_LZMA_MARKER, XX_ZIP_PY_LZMA_BAD };

typedef struct xx_zip_py_lzma_symbol {
    int kind;
    uint8_t byte;
    unsigned rep;
    uint32_t length, distance;
    bool whole; /* an end marker: the input held all of its bits */
} xx_zip_py_lzma_symbol;

static bool xx_zip_py_rc_normalize(xx_zip_py_rc *rc)
{
    if (rc->range >= 0x1000000u) return true;
    if (rc->starved || rc->position == rc->limit) {
        rc->starved = true;
        return false;
    }
    rc->range <<= 8;
    rc->code = (rc->code << 8) | rc->in[rc->position++];
    return true;
}

static unsigned xx_zip_py_rc_bit(xx_zip_py_rc *rc, uint16_t *probability)
{
    if (!xx_zip_py_rc_normalize(rc)) return 0;
    uint32_t bound = (rc->range >> 11) * *probability;
    if (rc->code < bound) {
        rc->range = bound;
        if (rc->update) *probability = (uint16_t)(*probability + ((2048 - *probability) >> 5));
        return 0;
    }
    rc->range -= bound;
    rc->code -= bound;
    if (rc->update) *probability = (uint16_t)(*probability - (*probability >> 5));
    return 1;
}

static unsigned xx_zip_py_rc_tree(xx_zip_py_rc *rc, uint16_t *probabilities, unsigned count)
{
    unsigned symbol = 1;
    for (unsigned i = 0; i < count; ++i) symbol = (symbol << 1) | xx_zip_py_rc_bit(rc, &probabilities[symbol]);
    return symbol - (1u << count);
}

static uint32_t xx_zip_py_rc_reverse(xx_zip_py_rc *rc, uint16_t *probabilities, unsigned offset, unsigned count)
{
    uint32_t result = 0;
    unsigned symbol = 1;
    for (unsigned i = 0; i < count; ++i) {
        unsigned bit = xx_zip_py_rc_bit(rc, &probabilities[offset + symbol - 1]);
        symbol = (symbol << 1) | bit;
        result |= (uint32_t)bit << i;
    }
    return result;
}

static uint32_t xx_zip_py_rc_direct(xx_zip_py_rc *rc, unsigned count)
{
    uint32_t result = 0;
    while (count--) {
        if (!xx_zip_py_rc_normalize(rc)) return 0;
        rc->range >>= 1;
        rc->code -= rc->range;
        uint32_t mask = 0u - (rc->code >> 31);
        rc->code += rc->range & mask;
        result = (result << 1) + (mask + 1);
    }
    return result;
}

static uint32_t xx_zip_py_lzma_len(xx_zip_py_rc *rc, xx_zip_py_lzma_length *coder, unsigned pos_state)
{
    if (!xx_zip_py_rc_bit(rc, &coder->choice)) return 2 + xx_zip_py_rc_tree(rc, coder->low[pos_state], 3);
    if (!xx_zip_py_rc_bit(rc, &coder->choice2)) return 10 + xx_zip_py_rc_tree(rc, coder->mid[pos_state], 3);
    return 18 + xx_zip_py_rc_tree(rc, coder->high, 8);
}

/* The byte `distance` + 1 back. */
static uint8_t xx_zip_py_lzma_back(const xx_zip_py_lzma *lz, uint32_t distance)
{
    size_t at = lz->at;
    if (at <= distance) at += (size_t)lz->dictionary_size;
    return lz->dictionary[at - distance - 1];
}

/* Decode one symbol; the decoder's state is not touched. */
static void xx_zip_py_lzma_decode(xx_zip_py_lzma *lz, xx_zip_py_rc *rc, xx_zip_py_lzma_symbol *out)
{
    unsigned state = lz->state, pos_state = (unsigned)(lz->total & ((1u << lz->pb) - 1));
    if (!xx_zip_py_rc_bit(rc, &lz->is_match[state][pos_state])) {
        unsigned previous = lz->total ? xx_zip_py_lzma_back(lz, 0) : 0, symbol = 1;
        uint16_t *probabilities =
            lz->literal + (size_t)0x300 * ((((size_t)lz->total & ((1u << lz->lp) - 1)) << lz->lc) + (previous >> (8 - lz->lc)));
        if (state < 7) {
            do symbol = (symbol << 1) | xx_zip_py_rc_bit(rc, &probabilities[symbol]);
            while (symbol < 0x100);
        } else {
            unsigned match = xx_zip_py_lzma_back(lz, lz->reps[0]), offset = 0x100;
            do {
                match <<= 1;
                unsigned match_bit = match & offset;
                unsigned bit = xx_zip_py_rc_bit(rc, &probabilities[offset + match_bit + symbol]);
                symbol = (symbol << 1) | bit;
                offset &= bit ? match_bit : ~match_bit;
            } while (symbol < 0x100);
        }
        out->kind = XX_ZIP_PY_LZMA_LITERAL;
        out->byte = (uint8_t)symbol;
        return;
    }
    if (!xx_zip_py_rc_bit(rc, &lz->is_rep[state])) {
        out->length = xx_zip_py_lzma_len(rc, &lz->match_length, pos_state);
        unsigned slot = xx_zip_py_rc_tree(rc, lz->slot[out->length < 6 ? out->length - 2 : 3], 6);
        uint32_t distance = slot;
        if (slot >= 4) {
            unsigned count = (slot >> 1) - 1;
            distance = (uint32_t)(2 | (slot & 1)) << count;
            if (slot < 14) {
                distance += xx_zip_py_rc_reverse(rc, lz->special, distance - slot, count);
            } else {
                distance += xx_zip_py_rc_direct(rc, count - 4) << 4;
                distance += xx_zip_py_rc_reverse(rc, lz->align, 0, 4);
            }
        }
        out->distance = distance;
        out->kind = XX_ZIP_PY_LZMA_MATCH;
        if (distance == 0xFFFFFFFFu) {
            out->kind = XX_ZIP_PY_LZMA_MARKER;
            out->whole = !rc->starved;
            xx_zip_py_rc_normalize(rc);
        }
        return;
    }
    /* liblzma: there cannot be a match if nothing has been produced */
    if (!lz->total) {
        out->kind = XX_ZIP_PY_LZMA_BAD;
        return;
    }
    if (!xx_zip_py_rc_bit(rc, &lz->is_rep_g0[state])) {
        if (!xx_zip_py_rc_bit(rc, &lz->is_rep0_long[state][pos_state])) {
            out->kind = XX_ZIP_PY_LZMA_SHORT_REP;
            return;
        }
        out->rep = 0;
    } else if (!xx_zip_py_rc_bit(rc, &lz->is_rep_g1[state])) {
        out->rep = 1;
    } else if (!xx_zip_py_rc_bit(rc, &lz->is_rep_g2[state])) {
        out->rep = 2;
    } else {
        out->rep = 3;
    }
    out->length = xx_zip_py_lzma_len(rc, &lz->rep_length, pos_state);
    out->kind = XX_ZIP_PY_LZMA_REP;
}

static void xx_zip_py_lzma_flush(xx_zip_py_lzma *lz, xx_zip_py_ext *file)
{
    while (lz->flushed < lz->total) {
        size_t at = (size_t)(lz->flushed % lz->dictionary_size);
        uint64_t count = lz->total - lz->flushed;
        if (count > lz->dictionary_size - at) count = lz->dictionary_size - at;
        xx_zip_py_emit(file, lz->dictionary + at, (size_t)count);
        lz->flushed += count;
    }
}

/* Room for the byte at lz->at: the dictionary's memory grows with the output,
 * up to the dictionary's size. */
static bool xx_zip_py_lzma_grow(xx_zip_py_lzma *lz)
{
    uint64_t larger = lz->capacity ? (uint64_t)lz->capacity * 2 : 65536;
    if (larger > lz->dictionary_size) larger = lz->dictionary_size;
    if (larger > SIZE_MAX) return false;
    uint8_t *grown = (uint8_t *)xx_rt_realloc(lz->dictionary, (size_t)larger);
    if (!grown) return false;
    lz->dictionary = grown;
    lz->capacity = (size_t)larger;
    return true;
}

static bool xx_zip_py_lzma_put(xx_zip_py_lzma *lz, uint8_t byte)
{
    if (lz->at == lz->capacity && !xx_zip_py_lzma_grow(lz)) return false;
    lz->dictionary[lz->at] = byte;
    if (++lz->at == lz->dictionary_size) lz->at = 0;
    ++lz->total;
    return true;
}

/* ---- the fast path of the LZMA decoder
 *
 * xx_zip_py_lzma_decode() above decides everything that depends on where the
 * input ends; it takes its input and writes its output a byte at a time. The
 * loop below decodes the same symbols with the same arithmetic while neither
 * end is near: at least XX_ZIP_PY_LZMA_FAST_INPUT bytes of input before each
 * symbol (no symbol takes more than 49, see xx_zip_py_lzma_run()), and room
 * in the dictionary's memory for the longest match (273 bytes) and one byte
 * more, so that the loop never fills the memory: it does not wrap, grow or
 * pass the size the caller allows. Under these
 * conditions no bit can find the input ended, so the tests for that are left
 * out, the coder's state lives in locals and a match is copied in one move.
 * A symbol that is not plain output (the end marker, a match with a distance
 * that is not valid) is decoded, probabilities included, exactly as the
 * careful decoder does it and handed to the caller unapplied. */

#define XX_ZIP_PY_LZMA_FAST_INPUT 64
#define XX_ZIP_PY_LZMA_FAST_OUTPUT 274

#define XX_ZIP_PY_FAST_NORMALIZE()      \
    if (range < 0x1000000u) {           \
        range <<= 8;                    \
        code = (code << 8) | *in++;     \
    }

/* One bit: the bound, the comparison and the three updates as the careful
 * decoder makes them, without a branch on the bit (which bit comes is what
 * a processor predicts worst here). */
#define XX_ZIP_PY_FAST_BIT(probability, bit)                                                                 \
    do {                                                                                                     \
        uint16_t *fast_p = (probability);                                                                    \
        uint32_t fast_v = *fast_p, fast_bound, fast_mask;                                                    \
        XX_ZIP_PY_FAST_NORMALIZE()                                                                           \
        fast_bound = (range >> 11) * fast_v;                                                                 \
        (bit) = code >= fast_bound;                                                                          \
        fast_mask = 0u - (uint32_t)(bit);                                                                    \
        range = fast_bound + ((range - 2 * fast_bound) & fast_mask);                                         \
        code -= fast_bound & fast_mask;                                                                      \
        *fast_p = (uint16_t)(fast_v + (((2048 - fast_v) >> 5) & ~fast_mask) - ((fast_v >> 5) & fast_mask));  \
    } while (0)

/* A bit tree of `count` levels: `result` gets the symbol with its top bit. */
#define XX_ZIP_PY_FAST_TREE(probabilities, count, result)                \
    do {                                                                 \
        uint16_t *fast_tree = (probabilities);                           \
        unsigned fast_symbol = 1, fast_bit;                              \
        for (unsigned fast_i = 0; fast_i < (count); ++fast_i) {          \
            XX_ZIP_PY_FAST_BIT(&fast_tree[fast_symbol], fast_bit);       \
            fast_symbol = (fast_symbol << 1) | fast_bit;                 \
        }                                                                \
        (result) = fast_symbol;                                          \
    } while (0)

#define XX_ZIP_PY_FAST_LENGTH(coder, result)                                             \
    do {                                                                                 \
        xx_zip_py_lzma_length *fast_coder = (coder);                                     \
        unsigned fast_choice, fast_value;                                                \
        XX_ZIP_PY_FAST_BIT(&fast_coder->choice, fast_choice);                            \
        if (!fast_choice) {                                                              \
            XX_ZIP_PY_FAST_TREE(fast_coder->low[pos_state], 3, fast_value);              \
            (result) = 2 + (fast_value - 8);                                             \
        } else {                                                                         \
            XX_ZIP_PY_FAST_BIT(&fast_coder->choice2, fast_choice);                       \
            if (!fast_choice) {                                                          \
                XX_ZIP_PY_FAST_TREE(fast_coder->mid[pos_state], 3, fast_value);          \
                (result) = 10 + (fast_value - 8);                                        \
            } else {                                                                     \
                XX_ZIP_PY_FAST_TREE(fast_coder->high, 8, fast_value);                    \
                (result) = 18 + (fast_value - 256);                                      \
            }                                                                            \
        }                                                                                \
    } while (0)

/* Whether the fast loop can take the next symbol: `left` is the number of
 * bytes the caller still allows. */
static bool xx_zip_py_lzma_fast_ready(const xx_zip_py_lzma *lz, const xx_zip_py_rc *rc, uint64_t left)
{
    /* at != 0: something has been produced and the byte before is in place */
    return lz->at && lz->capacity - lz->at >= XX_ZIP_PY_LZMA_FAST_OUTPUT && left >= XX_ZIP_PY_LZMA_FAST_OUTPUT &&
           rc->limit - rc->position >= XX_ZIP_PY_LZMA_FAST_INPUT;
}

/* Decode symbols into the dictionary while xx_zip_py_lzma_fast_ready() holds
 * (the caller has seen that it does). lz->at and lz->total move on by the
 * bytes produced, at most `left`. True: *out is a symbol the caller applies
 * as if xx_zip_py_lzma_decode() had given it; false: stopped before a
 * symbol. */
static bool xx_zip_py_lzma_fast(xx_zip_py_lzma *lz, xx_zip_py_rc *rc, uint64_t left, xx_zip_py_lzma_symbol *out)
{
    size_t at = lz->at, room = lz->capacity - at;
    if (left < room) room = (size_t)left;
    const size_t at_first = at, at_last = at + room - XX_ZIP_PY_LZMA_FAST_OUTPUT;
    const size_t dictionary_size = (size_t)lz->dictionary_size; /* capacity, and so this, fits a size_t once at != 0 */
    const size_t bias = (size_t)lz->total - at;                 /* at + bias: the low bits of the total */
    const uint8_t *in = rc->in + rc->position, *const in_last = rc->in + (rc->limit - XX_ZIP_PY_LZMA_FAST_INPUT);
    uint8_t *const dictionary = lz->dictionary;
    uint16_t *const literal = lz->literal;
    const unsigned lc = lz->lc;
    const size_t lp_mask = ((size_t)1 << lz->lp) - 1, pb_mask = ((size_t)1 << lz->pb) - 1;
    uint32_t range = rc->range, code = rc->code;
    uint32_t rep0 = lz->reps[0], rep1 = lz->reps[1], rep2 = lz->reps[2], rep3 = lz->reps[3];
    unsigned state = lz->state;
    bool special = false;

    while (at <= at_last && in <= in_last) {
        const unsigned pos_state = (unsigned)((at + bias) & pb_mask);
        unsigned bit;
        uint32_t length;
        XX_ZIP_PY_FAST_BIT(&lz->is_match[state][pos_state], bit);
        if (!bit) {
            uint16_t *probabilities = literal + (size_t)0x300 * ((((at + bias) & lp_mask) << lc) + ((size_t)dictionary[at - 1] >> (8 - lc)));
            unsigned symbol = 1;
            if (state < 7) {
                do {
                    XX_ZIP_PY_FAST_BIT(&probabilities[symbol], bit);
                    symbol = (symbol << 1) | bit;
                } while (symbol < 0x100);
            } else {
                unsigned match = dictionary[rep0 < at ? at - rep0 - 1 : at + (dictionary_size - rep0) - 1], offset = 0x100;
                do {
                    match <<= 1;
                    unsigned match_bit = match & offset;
                    XX_ZIP_PY_FAST_BIT(&probabilities[offset + match_bit + symbol], bit);
                    symbol = (symbol << 1) | bit;
                    offset &= bit ? match_bit : ~match_bit;
                } while (symbol < 0x100);
            }
            dictionary[at++] = (uint8_t)symbol;
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        XX_ZIP_PY_FAST_BIT(&lz->is_rep[state], bit);
        if (!bit) {
            unsigned slot;
            uint32_t distance;
            XX_ZIP_PY_FAST_LENGTH(&lz->match_length, length);
            XX_ZIP_PY_FAST_TREE(lz->slot[length < 6 ? length - 2 : 3], 6, slot);
            slot -= 64;
            distance = slot;
            if (slot >= 4) {
                unsigned count = (slot >> 1) - 1, symbol = 1;
                distance = (uint32_t)(2 | (slot & 1)) << count;
                if (slot < 14) {
                    uint16_t *probabilities = lz->special + (distance - slot);
                    for (unsigned i = 0; i < count; ++i) {
                        XX_ZIP_PY_FAST_BIT(&probabilities[symbol - 1], bit);
                        symbol = (symbol << 1) | bit;
                        distance += (uint32_t)bit << i;
                    }
                } else {
                    uint32_t direct = 0;
                    for (count -= 4; count; --count) {
                        XX_ZIP_PY_FAST_NORMALIZE()
                        range >>= 1;
                        code -= range;
                        uint32_t mask = 0u - (code >> 31);
                        code += range & mask;
                        direct = (direct << 1) + (mask + 1);
                    }
                    distance += direct << 4;
                    for (unsigned i = 0; i < 4; ++i) {
                        XX_ZIP_PY_FAST_BIT(&lz->align[symbol - 1], bit);
                        symbol = (symbol << 1) | bit;
                        distance += (uint32_t)bit << i;
                    }
                }
            }
            if (distance == 0xFFFFFFFFu) {
                XX_ZIP_PY_FAST_NORMALIZE()
                out->kind = XX_ZIP_PY_LZMA_MARKER;
                out->whole = true;
                out->length = length;
                out->distance = distance;
                special = true;
                break;
            }
            if (distance >= lz->total + (at - at_first) || distance >= lz->dictionary_size) {
                out->kind = XX_ZIP_PY_LZMA_MATCH;
                out->length = length;
                out->distance = distance;
                special = true;
                break;
            }
            rep3 = rep2;
            rep2 = rep1;
            rep1 = rep0;
            rep0 = distance;
            state = state < 7 ? 7 : 10;
        } else {
            XX_ZIP_PY_FAST_BIT(&lz->is_rep_g0[state], bit);
            if (!bit) {
                XX_ZIP_PY_FAST_BIT(&lz->is_rep0_long[state][pos_state], bit);
                if (!bit) {
                    dictionary[at] = dictionary[rep0 < at ? at - rep0 - 1 : at + (dictionary_size - rep0) - 1];
                    ++at;
                    state = state < 7 ? 9 : 11;
                    continue;
                }
            } else {
                uint32_t distance;
                XX_ZIP_PY_FAST_BIT(&lz->is_rep_g1[state], bit);
                if (!bit) {
                    distance = rep1;
                } else {
                    XX_ZIP_PY_FAST_BIT(&lz->is_rep_g2[state], bit);
                    if (!bit) {
                        distance = rep2;
                    } else {
                        distance = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = distance;
            }
            XX_ZIP_PY_FAST_LENGTH(&lz->rep_length, length);
            state = state < 7 ? 8 : 11;
        }
        /* `length` bytes from rep0 + 1 back, each one as the careful decoder
           would read it: a byte this copy has written can be its source */
        if (rep0 < at) {
            const uint8_t *from = dictionary + at - rep0 - 1;
            uint8_t *to = dictionary + at;
            if (rep0 + 1 >= length) {
                memcpy(to, from, length);
            } else if (!rep0) {
                memset(to, *from, length);
            } else {
                for (uint32_t i = 0; i < length; ++i) to[i] = from[i];
            }
            at += length;
        } else {
            /* the source starts before the dictionary's wrap */
            size_t from = at + (dictionary_size - rep0) - 1;
            for (uint32_t i = 0; i < length; ++i) {
                dictionary[at++] = dictionary[from];
                if (++from == dictionary_size) from = 0;
            }
        }
    }
    rc->range = range;
    rc->code = code;
    rc->position = (size_t)(in - rc->in);
    lz->state = state;
    lz->reps[0] = rep0;
    lz->reps[1] = rep1;
    lz->reps[2] = rep2;
    lz->reps[3] = rep3;
    lz->total += at - at_first;
    lz->at = at;
    return special;
}

#undef XX_ZIP_PY_FAST_LENGTH
#undef XX_ZIP_PY_FAST_TREE
#undef XX_ZIP_PY_FAST_BIT
#undef XX_ZIP_PY_FAST_NORMALIZE

/* lzma_code() of the raw decoder over the input fed so far, with unlimited
 * room: every symbol the input holds completely. */
static xx_zip_py_step xx_zip_py_lzma_run(xx_zip_py_lzma *lz, xx_zip_py_ext *file)
{
    const uint8_t *in = file->input;
    size_t limit = file->fed;
    xx_zip_py_step step;
    while (lz->init_left) {
        if (lz->position == limit) return XX_ZIP_PY_STEP_INPUT;
        if (lz->init_left == 5 && in[lz->position]) return XX_ZIP_PY_STEP_ERROR;
        lz->code = (lz->code << 8) | in[lz->position++];
        --lz->init_left;
    }
    for (;;) {
        xx_zip_py_lzma_symbol symbol = {0};
        xx_zip_py_rc rc = {lz->range, lz->code, in, lz->position, limit, false, false};
        bool decoded = false;
        if (lz->total - lz->flushed >= 2048) xx_zip_py_lzma_flush(lz, file);
        /* The range coder takes at most one byte before each bit, and no
           symbol has more than 49 of those points (a match: 2 bits, 10 for
           the length, 6 for the slot, 26 direct ones, 4 aligned ones, and
           the end marker's last look). So 64 bytes always hold a whole
           symbol: the fast loop takes symbols while there are that many.
           Nearer the end of the input, see first whether the symbol is all
           there: if not, liblzma waits, with nothing changed that shows. */
        if (xx_zip_py_lzma_fast_ready(lz, &rc, UINT64_MAX)) {
            /* The loop can fill the dictionary's memory up to its end: what
               is waiting there from before the last wrap goes out first, and
               what the loop gives goes out before a symbol from the careful
               decoder can wrap again. */
            xx_zip_py_lzma_flush(lz, file);
            rc.update = true;
            decoded = xx_zip_py_lzma_fast(lz, &rc, UINT64_MAX, &symbol);
            lz->range = rc.range;
            lz->code = rc.code;
            lz->position = rc.position;
            xx_zip_py_lzma_flush(lz, file);
        }
        if (!decoded) {
            rc.update = false;
            if (limit - lz->position < 64) {
                xx_zip_py_lzma_decode(lz, &rc, &symbol);
                if (rc.starved) {
                    step = XX_ZIP_PY_STEP_INPUT;
                    break;
                }
                rc.range = lz->range;
                rc.code = lz->code;
                rc.position = lz->position;
            }
            rc.update = true;
            xx_zip_py_lzma_decode(lz, &rc, &symbol);
            lz->range = rc.range;
            lz->code = rc.code;
            lz->position = rc.position;
        }
        step = XX_ZIP_PY_STEP_ERROR;
        if (rc.starved || symbol.kind == XX_ZIP_PY_LZMA_BAD) break;
        if (symbol.kind == XX_ZIP_PY_LZMA_MARKER) {
            if (!lz->code) step = XX_ZIP_PY_STEP_END;
            break;
        }
        if (symbol.kind == XX_ZIP_PY_LZMA_LITERAL) {
            lz->state = lz->state < 4 ? 0 : lz->state < 10 ? lz->state - 3 : lz->state - 6;
            if (!xx_zip_py_lzma_put(lz, symbol.byte)) {
                step = XX_ZIP_PY_STEP_MEMORY;
                break;
            }
            continue;
        }
        if (symbol.kind == XX_ZIP_PY_LZMA_MATCH) {
            lz->reps[3] = lz->reps[2];
            lz->reps[2] = lz->reps[1];
            lz->reps[1] = lz->reps[0];
            lz->reps[0] = symbol.distance;
            lz->state = lz->state < 7 ? 7 : 10;
            if (symbol.distance >= lz->total || symbol.distance >= lz->dictionary_size) break;
        } else if (symbol.kind == XX_ZIP_PY_LZMA_SHORT_REP) {
            lz->state = lz->state < 7 ? 9 : 11;
            symbol.length = 1;
        } else {
            uint32_t distance = lz->reps[symbol.rep];
            for (unsigned i = symbol.rep; i; --i) lz->reps[i] = lz->reps[i - 1];
            lz->reps[0] = distance;
            lz->state = lz->state < 7 ? 8 : 11;
        }
        step = XX_ZIP_PY_STEP_INPUT;
        for (uint32_t i = 0; i < symbol.length; ++i) {
            if (!xx_zip_py_lzma_put(lz, xx_zip_py_lzma_back(lz, lz->reps[0]))) {
                step = XX_ZIP_PY_STEP_MEMORY;
                break;
            }
        }
        if (step == XX_ZIP_PY_STEP_MEMORY) break;
    }
    xx_zip_py_lzma_flush(lz, file);
    return step;
}

static void xx_zip_py_lzma_reset(uint16_t *probabilities, size_t count)
{
    for (size_t i = 0; i < count; ++i) probabilities[i] = 1024;
}

static void xx_zip_py_lzma_length_reset(xx_zip_py_lzma_length *coder)
{
    coder->choice = coder->choice2 = 1024;
    xx_zip_py_lzma_reset(&coder->low[0][0], sizeof(coder->low) / sizeof(uint16_t));
    xx_zip_py_lzma_reset(&coder->mid[0][0], sizeof(coder->mid) / sizeof(uint16_t));
    xx_zip_py_lzma_reset(coder->high, 256);
}

/* The decoder's state for properties lc, lp, pb, as at the start of a stream. */
static void xx_zip_py_lzma_reset_state(xx_zip_py_lzma *lz)
{
    xx_zip_py_lzma_reset(lz->literal, (size_t)0x300 << (lz->lc + lz->lp));
    xx_zip_py_lzma_reset(&lz->is_match[0][0], sizeof(lz->is_match) / sizeof(uint16_t));
    xx_zip_py_lzma_reset(lz->is_rep, 12);
    xx_zip_py_lzma_reset(lz->is_rep_g0, 12);
    xx_zip_py_lzma_reset(lz->is_rep_g1, 12);
    xx_zip_py_lzma_reset(lz->is_rep_g2, 12);
    xx_zip_py_lzma_reset(&lz->is_rep0_long[0][0], sizeof(lz->is_rep0_long) / sizeof(uint16_t));
    xx_zip_py_lzma_reset(&lz->slot[0][0], sizeof(lz->slot) / sizeof(uint16_t));
    xx_zip_py_lzma_reset(lz->special, 128);
    xx_zip_py_lzma_reset(lz->align, 16);
    xx_zip_py_lzma_length_reset(&lz->match_length);
    xx_zip_py_lzma_length_reset(&lz->rep_length);
    lz->state = 0;
    lz->reps[0] = lz->reps[1] = lz->reps[2] = lz->reps[3] = 0;
}

/* zipfile.LZMADecompressor.decompress(): nothing until more than the header
 * (version, properties size, properties) is there, then the raw decoder. */
static xx_zip_py_step xx_zip_py_lzma_feed(xx_zip_py_lzma *lz, xx_zip_py_ext *file)
{
    if (!lz->started) {
        const uint8_t *in = file->input;
        if (file->fed <= 4) return XX_ZIP_PY_STEP_INPUT;
        size_t size = xx_zip_py_rd16(in + 2);
        if (file->fed <= 4 + size) return XX_ZIP_PY_STEP_INPUT;
        /* lzma._decode_filter_properties(): LZMA_OPTIONS_ERROR */
        unsigned byte = in[4];
        lz->message = "Invalid or unsupported options";
        if (size != 5 || byte > (4 * 5 + 4) * 9 + 8) return XX_ZIP_PY_STEP_ERROR;
        lz->pb = byte / 45;
        lz->lp = (byte % 45) / 9;
        lz->lc = byte % 9;
        if (lz->lc + lz->lp > 4) return XX_ZIP_PY_STEP_ERROR;
        lz->message = "Corrupt input data";
        /* liblzma's dictionary: at least 4096 bytes, a multiple of 16 */
        lz->dictionary_size = xx_zip_py_rd32(in + 5);
        if (lz->dictionary_size < 4096) lz->dictionary_size = 4096;
        lz->dictionary_size = (lz->dictionary_size + 15) & ~(uint64_t)15;
        size_t count = (size_t)0x300 << (lz->lc + lz->lp);
        lz->literal = (uint16_t *)xx_rt_malloc(count * sizeof(uint16_t));
        if (!lz->literal) return XX_ZIP_PY_STEP_MEMORY;
        xx_zip_py_lzma_reset_state(lz);
        lz->range = 0xFFFFFFFFu;
        lz->init_left = 5;
        lz->position = 4 + size;
        lz->started = true;
    }
    return xx_zip_py_lzma_run(lz, file);
}

/* ---- ZipExtFile._read2(), _read1(), read() */

static xx_zip_py_status xx_zip_py_check_crc(const xx_zip_py_info *info, uint32_t crc, char **error)
{
    if (crc == info->crc) return XX_ZIP_PY_OK;
    return xx_zip_py_fail(error, XX_ZIP_PY_BAD, xx_zip_py_message("Bad CRC-32 for file ", info->filename, strlen(info->filename), ""));
}

/* _read2(n): at least MIN_READ_SIZE and at most _compress_left bytes of the
 * file; EOFError when the file has none. */
static xx_zip_py_status xx_zip_py_read2(xx_zip_py_ext *file, uint64_t n, char **error)
{
    if (!file->compress_left) return XX_ZIP_PY_OK;
    if (n < XX_ZIP_PY_MIN_READ_SIZE) n = XX_ZIP_PY_MIN_READ_SIZE;
    if (n > file->compress_left) n = file->compress_left;
    size_t rest = file->available - file->fed, got = n < rest ? (size_t)n : rest;
    file->compress_left -= got;
    if (!got) return xx_zip_py_fail_text(error, XX_ZIP_PY_DATA_ERROR, "EOFError");
    file->fed += got;
    return XX_ZIP_PY_OK;
}

/* _read1(n) of a compressed member: *length bytes were added to the sink. */
static xx_zip_py_status xx_zip_py_read1(xx_zip_py_ext *file, uint64_t n, uint64_t *length, char **error)
{
    xx_zip_py_status status;
    xx_zip_py_step step;
    file->produced = 0;
    if (file->inflate) {
        /* data = unconsumed_tail, more of the file if that is shorter than
           n; decompress(data, max(n, MIN_READ_SIZE)) */
        uint64_t tail = file->fed - file->consumed;
        if (n > tail && (status = xx_zip_py_read2(file, n - tail, error)) != XX_ZIP_PY_OK) return status;
        uint64_t most = n < XX_ZIP_PY_MIN_READ_SIZE ? XX_ZIP_PY_MIN_READ_SIZE : n;
        step = xx_zip_py_inflate_run(file->inflate, file, file->inflate->total + most);
        if (step == XX_ZIP_PY_STEP_ERROR)
            return xx_zip_py_fail(error, XX_ZIP_PY_DATA_ERROR,
                                  xx_zip_py_concat("Error -3 while decompressing data: ", file->inflate->message, ""));
        file->consumed = step == XX_ZIP_PY_STEP_INPUT ? file->fed : (size_t)((file->inflate->position + 7) >> 3);
        file->eof = step == XX_ZIP_PY_STEP_END || (!file->compress_left && file->consumed == file->fed);
        /* data += flush(): whatever zlib still holds, errors ignored */
        if (file->eof && step != XX_ZIP_PY_STEP_END) step = xx_zip_py_inflate_run(file->inflate, file, UINT64_MAX);
        if (step == XX_ZIP_PY_STEP_ERROR) step = XX_ZIP_PY_STEP_INPUT;
    } else {
        if ((status = xx_zip_py_read2(file, n, error)) != XX_ZIP_PY_OK) return status;
        if (file->bzip2) {
            step = xx_zip_py_bzip2_run(file->bzip2, file);
            if (step == XX_ZIP_PY_STEP_ERROR) return xx_zip_py_fail_text(error, XX_ZIP_PY_DATA_ERROR, "Invalid data stream");
        } else {
            step = xx_zip_py_lzma_feed(file->lzma, file);
            if (step == XX_ZIP_PY_STEP_ERROR) return xx_zip_py_fail_text(error, XX_ZIP_PY_DATA_ERROR, file->lzma->message);
        }
        file->eof = step == XX_ZIP_PY_STEP_END || !file->compress_left;
    }
    if (step == XX_ZIP_PY_STEP_MEMORY || file->no_memory) return xx_zip_py_fail_text(error, XX_ZIP_PY_NO_MEMORY, "out of memory");
    /* data = data[:_left] */
    *length = file->produced < file->left ? file->produced : file->left;
    file->left -= *length;
    if (!file->left) file->eof = true;
    return file->eof ? xx_zip_py_check_crc(file->info, file->sink.crc, error) : XX_ZIP_PY_OK;
}

static void xx_zip_py_ext_free(xx_zip_py_ext *file)
{
    xx_rt_free(file->inflate);
    if (file->bzip2) xx_rt_free(file->bzip2->tt);
    xx_rt_free(file->bzip2);
    if (file->lzma) {
        xx_rt_free(file->lzma->dictionary);
        xx_rt_free(file->lzma->literal);
    }
    xx_rt_free(file->lzma);
    xx_zip_py_sink_free(&file->sink);
}

/* read(limit) and/or read() of a stored member (see xx_zip_py_read_steps):
 * the bytes are the archive's own. */
static xx_zip_py_status xx_zip_py_read_stored(const xx_zip_py_info *info, const uint8_t *input, size_t available, size_t limit,
                                              bool rest, size_t *out_size, bool *complete, char **error)
{
    uint64_t compress_left = info->compress_size, left = info->file_size, n = limit;
    size_t fed = 0, kept = 0;
    bool eof = false;
    for (int all = limit ? 0 : 1; all <= (rest ? 1 : 0); ++all) {
        while (!eof && (all || n)) {
            size_t got = 0;
            if (compress_left) {
                uint64_t count = all ? XX_ZIP_PY_MAX_N : n;
                if (count < XX_ZIP_PY_MIN_READ_SIZE) count = XX_ZIP_PY_MIN_READ_SIZE;
                if (count > compress_left) count = compress_left;
                got = count < available - fed ? (size_t)count : available - fed;
                compress_left -= got;
                if (!got) return xx_zip_py_fail_text(error, XX_ZIP_PY_DATA_ERROR, "EOFError");
                fed += got;
            }
            eof = !compress_left;
            size_t length = got < left ? got : (size_t)left;
            left -= length;
            if (!left) eof = true;
            kept += length;
            if (eof) {
                xx_zip_py_status status = xx_zip_py_check_crc(info, xx_crc32_calc(0, input, kept), error);
                if (status != XX_ZIP_PY_OK) return status;
            }
            if (!all) n = n < length ? 0 : n - length;
        }
    }
    *complete = eof;
    *out_size = eof ? kept : limit;
    return XX_ZIP_PY_OK;
}

/* The most output a member's data can decompress to, for the reservation
   only: Deflate expands at most 1032:1. Others can go further; their output
   then moves from the reservation to the heap (one copy). */
static size_t xx_zip_py_output_bound(size_t input_size)
{
    size_t slack = (size_t)1 << 20;
    if (input_size > (SIZE_MAX - slack) / 1032) return SIZE_MAX;
    return input_size * 1032 + slack;
}

/* Memory for a whole member before decoding it: one allocation of the
 * declared size instead of growing by doubling. A larger member gets address
 * space for as much as its data can plausibly hold, committed as the output
 * arrives: a lying size costs no memory, and the output is never copied
 * while it grows. */
static void xx_zip_py_sink_prepare(xx_zip_py_sink *sink, size_t cap, size_t input_size)
{
    size_t plausible = xx_zip_py_output_bound(input_size);
    xx_zip_py_sink_init(sink, cap);
    sink->checksum = true;
    if (plausible > cap) plausible = cap;
    if (plausible <= XX_ZIP_PY_RESERVE_MAX || !xx_zip_py_sink_reserve_space(sink, plausible))
        xx_zip_py_sink_reserve(sink, cap < XX_ZIP_PY_RESERVE_MAX ? cap : XX_ZIP_PY_RESERVE_MAX);
}

/* One ZipExtFile from the member's start: read(limit) when limit is not 0,
 * then read() when `rest` is set. */
static xx_zip_py_status xx_zip_py_read_steps(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t data_offset,
                                             size_t limit, bool rest, uint8_t **out, size_t *out_size, bool *complete,
                                             xx_zip_py_owner *owner, char **error)
{
    *out = NULL;
    *out_size = 0;
    *complete = false;
    *owner = XX_ZIP_PY_OWNED;
    if (error) *error = NULL;

    size_t available = data_offset < zip->size ? zip->size - (size_t)data_offset : 0;
    const uint8_t *input = zip->data + (data_offset < zip->size ? data_offset : zip->size);
    size_t input_size = info->compress_size > available ? available : (size_t)info->compress_size;
    size_t target = info->file_size > SIZE_MAX ? SIZE_MAX : (size_t)info->file_size;
    xx_zip_py_status status;

    if (info->compress_type == XX_ZIP_PY_STORED) {
        status = xx_zip_py_read_stored(info, input, available, limit, rest, out_size, complete, error);
        if (status != XX_ZIP_PY_OK) return status;
        /* The bytes already sit uncompressed in the archive: hand them out
           in place, no copy. */
        *out = (uint8_t *)input;
        *owner = XX_ZIP_PY_BORROWED;
        return XX_ZIP_PY_OK;
    }

    xx_zip_py_ext file;
    memset(&file, 0, sizeof(file));
    bool mapped = false, deflated = info->compress_type == XX_ZIP_PY_DEFLATED;
    file.info = info;
    file.input = input;
    file.available = available;
    file.compress_left = info->compress_size;
    file.left = info->file_size;
    if (rest && !deflated) {
        xx_zip_py_sink_prepare(&file.sink, target, input_size);
    } else {
        xx_zip_py_sink_init(&file.sink, target);
        file.sink.checksum = true;
    }
    if (deflated) {
        /* the window needs no clearing: it is never read before written */
        file.inflate = (xx_zip_py_inflate *)xx_rt_malloc(sizeof(*file.inflate));
        if (file.inflate) memset(file.inflate, 0, offsetof(xx_zip_py_inflate, window));
    } else if (info->compress_type == XX_ZIP_PY_BZIP2) {
        file.bzip2 = xx_zip_py_bzip2_new();
    } else {
        file.lzma = (xx_zip_py_lzma *)xx_rt_malloc(sizeof(*file.lzma));
        if (file.lzma) memset(file.lzma, 0, sizeof(*file.lzma));
    }
    if (!file.inflate && !file.bzip2 && !file.lzma) {
        xx_zip_py_ext_free(&file);
        return xx_zip_py_fail_text(error, XX_ZIP_PY_NO_MEMORY, "out of memory");
    }

    status = XX_ZIP_PY_OK;
    uint64_t n = limit, length = 0;
    /* read(limit): until that many bytes have come or the end is seen; what
       a call returns beyond them stays in ZipExtFile's buffer */
    while (n && !file.eof && (status = xx_zip_py_read1(&file, n, &length, error)) == XX_ZIP_PY_OK) n = n < length ? 0 : n - length;

    /* The rest of a Deflate member whose data is all there: when the fast
       decoder reaches the end of the stream within the declared size and
       the CRC-32 is right, zlib gives the same bytes and reports the end,
       however the reads cut the data (of any size: ZipExtFile takes MAX_N
       bytes at a time, and no piece of a sound stream fails). In every
       other case the steps go on; they tell data that ends early from data
       zlib rejects and give zlib's message. The fast decoder runs in its
       zlib-strict mode, which fails the block headers zlib fails (a code
       length set that leaves codes unused, no end-of-block code); whatever
       else zlib rejects fails in the fast decoder in every mode. So
       read() alone needs no trial of the first chunk through the steps. */
    if (status == XX_ZIP_PY_OK && rest && !file.eof && deflated && input_size && info->compress_size <= available) {
        {
            xx_zip_py_sink whole;
            size_t consumed = 0;
            xx_zip_py_sink_prepare(&whole, target, input_size);
            bool ok = xx_deflate_unpack_memory_to_device_flags(input, input_size, &whole.device, &consumed, false,
                                                               XX_DEFLATE_UNPACK_ZLIB_STRICT, NULL);
            if (whole.no_memory) {
                xx_zip_py_sink_free(&whole);
                xx_zip_py_ext_free(&file);
                return xx_zip_py_fail_text(error, XX_ZIP_PY_NO_MEMORY, "out of memory");
            }
            if (ok && !whole.full && whole.crc == info->crc) {
                xx_zip_py_ext_free(&file);
                *out = xx_zip_py_sink_detach(&whole, out_size, &mapped);
                if (!*out) return xx_zip_py_fail_text(error, XX_ZIP_PY_NO_MEMORY, "out of memory");
                *owner = mapped ? XX_ZIP_PY_MAPPED : XX_ZIP_PY_OWNED;
                *complete = true;
                return XX_ZIP_PY_OK;
            }
            xx_zip_py_sink_free(&whole);
        }
    }

    /* read(): the buffer, then MAX_N at a time until the end is seen */
    while (rest && status == XX_ZIP_PY_OK && !file.eof) status = xx_zip_py_read1(&file, XX_ZIP_PY_MAX_N, &length, error);
    if (status != XX_ZIP_PY_OK) {
        xx_zip_py_ext_free(&file);
        return status;
    }
    *complete = file.eof;
    if (!file.eof) file.sink.size = limit;
    *out = xx_zip_py_sink_detach(&file.sink, out_size, &mapped);
    xx_zip_py_ext_free(&file);
    if (!*out) return xx_zip_py_fail_text(error, XX_ZIP_PY_NO_MEMORY, "out of memory");
    *owner = mapped ? XX_ZIP_PY_MAPPED : XX_ZIP_PY_OWNED;
    return XX_ZIP_PY_OK;
}

xx_zip_py_status xx_zip_py_read(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t data_offset, size_t limit,
                                uint8_t **out, size_t *out_size, bool *complete, xx_zip_py_owner *owner, char **error)
{
    return xx_zip_py_read_steps(zip, info, data_offset, limit, !limit, out, out_size, complete, owner, error);
}

xx_zip_py_status xx_zip_py_read_rest(const xx_zip_py *zip, const xx_zip_py_info *info, uint64_t data_offset, size_t first,
                                     uint8_t **out, size_t *out_size, xx_zip_py_owner *owner, char **error)
{
    bool complete;
    return xx_zip_py_read_steps(zip, info, data_offset, first, true, out, out_size, &complete, owner, error);
}

bool xx_zip_py_raw_data(const xx_zip_py *zip, const xx_zip_py_info *info, const uint8_t **data, size_t *size)
{
    if (info->header_offset < 0 || (uint64_t)info->header_offset > zip->size ||
        zip->size - (size_t)info->header_offset < XX_ZIP_PY_SIZE_FILE_HEADER)
        return false;
    const uint8_t *header = zip->data + info->header_offset;
    uint64_t offset =
        (uint64_t)info->header_offset + XX_ZIP_PY_SIZE_FILE_HEADER + xx_zip_py_rd16(header + 26) + xx_zip_py_rd16(header + 28);
    if (offset > zip->size) offset = zip->size;
    size_t available = zip->size - (size_t)offset;
    *data = zip->data + offset;
    *size = info->compress_size < available ? (size_t)info->compress_size : available;
    return true;
}

/* ------------------------------------------------------- lzma.decompress */

/* lzma.decompress(data) is a loop over LZMADecompressor objects (format
 * FORMAT_AUTO), each of which takes one stream from what is left:
 *   - the decoder's own error is raised when it comes from the first
 *     stream; from a later one it ends the loop, and what was decoded
 *     before is returned;
 *   - a decoder that does not get to the end of its stream, the first or a
 *     later one, is LZMAError("Compressed data ended before the
 *     end-of-stream marker was reached"), and nothing is returned. So is no
 *     data at all, and so are a few bytes after a stream in which liblzma
 *     has found no fault yet (zeros, the start of a magic).
 * liblzma takes its input byte by byte and reports a fault where it gets to
 * it, so which of the two a damaged and cut stream gives depends on where
 * the data ends. The decoders below go through a stream in liblzma's order
 * and tell, at every step, the end of the data from a fault, with the
 * fault's text: LZMA_FORMAT_ERROR, LZMA_DATA_ERROR and LZMA_OPTIONS_ERROR as
 * CPython words them. */

#define XX_ZIP_PY_XZ_FILTER_LZMA2 0x21

static const uint8_t xx_zip_py_xz_magic[6] = {0xFD, '7', 'z', 'X', 'Z', 0x00};
#define XX_ZIP_PY_XZ_VLI_MAX (UINT64_MAX / 2)
#define XX_ZIP_PY_XZ_UNKNOWN UINT64_MAX

static const char xx_zip_py_xz_ended[] = "Compressed data ended before the end-of-stream marker was reached";
static const char xx_zip_py_xz_format[] = "Input format not supported by decoder";
static const char xx_zip_py_xz_data[] = "Corrupt input data";
static const char xx_zip_py_xz_options[] = "Invalid or unsupported options";
static const char xx_zip_py_xz_memory[] = "Cannot allocate memory";

typedef enum xx_zip_py_xz_step {
    XX_ZIP_PY_XZ_END,  /* decoded to its end */
    XX_ZIP_PY_XZ_MORE, /* the data ends first: liblzma waits for more */
    XX_ZIP_PY_XZ_FAIL  /* liblzma's error (message) */
} xx_zip_py_xz_step;

/* A filter before LZMA2 in a block's chain, as liblzma's decoder holds it:
 * the delta filter's history, or a BCJ filter ("simple" coder) with the
 * bytes it could not convert yet. */
#define XX_ZIP_PY_XZ_FILTER_DELTA 0x03
#define XX_ZIP_PY_XZ_FILTER_X86 0x04
#define XX_ZIP_PY_XZ_FILTER_POWERPC 0x05
#define XX_ZIP_PY_XZ_FILTER_IA64 0x06
#define XX_ZIP_PY_XZ_FILTER_ARM 0x07
#define XX_ZIP_PY_XZ_FILTER_ARMTHUMB 0x08
#define XX_ZIP_PY_XZ_FILTER_SPARC 0x09

typedef struct xx_zip_py_xz_filter {
    unsigned id;
    /* delta */
    unsigned distance;
    uint8_t delta_pos;
    uint8_t history[256];
    /* BCJ: lzma_simple_coder */
    uint32_t now_pos;             /* the stream position of the next byte to convert */
    uint32_t prev_mask, prev_pos; /* x86 */
    bool end_was_reached;
    size_t pos, filtered, size, allocated; /* buffer[pos .. filtered) is converted, [filtered .. size) is not */
    uint8_t buffer[32];
} xx_zip_py_xz_filter;

typedef struct xx_zip_py_xz {
    xx_zip_py_lzma lz;
    xx_buf_t *out;
    const char *message;  /* of a failure */
    /* the block's filters before LZMA2, and what LZMA2 gave for them */
    unsigned filter_count;
    xx_zip_py_xz_filter filters[3];
    size_t raw_at, raw_end; /* LZMA2's bytes not yet taken by the filters: positions in the stream's output */
    bool raw_ended;         /* LZMA2 reached its end marker */
    bool raw_starved;       /* a filter asked for more than LZMA2 has: liblzma has used all its input */
    /* CPython's output buffer for the stream being decoded */
    size_t stream_start;  /* where the stream's output starts in out */
    unsigned py_block;    /* the index of the buffer's last block */
    size_t py_end;        /* the end of that block, from stream_start */
    size_t pending_size;
    uint8_t pending[4096]; /* decoded bytes on their way to out */
} xx_zip_py_xz;

static xx_zip_py_xz_step xx_zip_py_xz_fail(xx_zip_py_xz *xz, const char *message)
{
    xz->message = message;
    return XX_ZIP_PY_XZ_FAIL;
}

static bool xx_zip_py_xz_flush(xx_zip_py_xz *xz)
{
    bool ok = !xz->pending_size || xx_buf_append(xz->out, xz->pending, xz->pending_size);
    xz->pending_size = 0;
    return ok;
}

/* One decoded byte: into the dictionary and on to the output. */
static bool xx_zip_py_xz_put(xx_zip_py_xz *xz, uint8_t byte)
{
    if (!xx_zip_py_lzma_put(&xz->lz, byte)) return false;
    xz->pending[xz->pending_size++] = byte;
    return xz->pending_size < sizeof(xz->pending) || xx_zip_py_xz_flush(xz);
}

/* `count` bytes that are not coded (an uncompressed chunk): into the
 * dictionary and on to the output. */
static bool xx_zip_py_xz_put_bytes(xx_zip_py_xz *xz, const uint8_t *bytes, size_t count)
{
    xx_zip_py_lzma *lz = &xz->lz;
    if (!xx_zip_py_xz_flush(xz) || (count && !xx_buf_append(xz->out, bytes, count))) return false;
    while (count) {
        if (lz->at == lz->capacity && !xx_zip_py_lzma_grow(lz)) return false;
        size_t part = lz->capacity - lz->at;
        if (part > count) part = count;
        memcpy(lz->dictionary + lz->at, bytes, part);
        lz->at += part;
        if (lz->at == lz->dictionary_size) lz->at = 0;
        lz->total += part;
        bytes += part;
        count -= part;
    }
    return true;
}

/* The dictionary liblzma keeps for a declared size: at least 4096 bytes, a
 * multiple of 16. The decoder state starts anew. */
static void xx_zip_py_xz_lzma_start(xx_zip_py_xz *xz, uint64_t dictionary_size, unsigned properties)
{
    xx_zip_py_lzma *lz = &xz->lz;
    if (dictionary_size < 4096) dictionary_size = 4096;
    lz->dictionary_size = (dictionary_size + 15) & ~(uint64_t)15;
    if (lz->capacity > lz->dictionary_size) lz->capacity = (size_t)lz->dictionary_size;
    lz->total = 0;
    lz->at = 0;
    lz->pb = properties / 45;
    lz->lp = (properties % 45) / 9;
    lz->lc = properties % 9;
    xx_zip_py_lzma_reset_state(lz);
}

/* liblzma's LZMA decoder over data[*position .. size): `left` bytes to
 * decode, or XX_ZIP_PY_XZ_UNKNOWN for data that ends with the end marker.
 * A known size allows no marker; when it is reached the range coder takes
 * the byte it still needs and must be left with the code 0. On
 * XX_ZIP_PY_XZ_MORE liblzma has taken every byte there is. */
static xx_zip_py_xz_step xx_zip_py_xz_lzma(xx_zip_py_xz *xz, const uint8_t *data, size_t size, size_t *position, uint64_t left)
{
    xx_zip_py_lzma *lz = &xz->lz;
    xx_zip_py_rc rc = {0xFFFFFFFFu, 0, data, *position, size, true, false};
    for (unsigned i = 0; i < 5; ++i) {
        if (rc.position == size) return XX_ZIP_PY_XZ_MORE;
        if (!i && data[rc.position]) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
        rc.code = (rc.code << 8) | data[rc.position++];
    }
    for (;;) {
        xx_zip_py_lzma_symbol symbol = {0};
        if (!left) {
            if (!xx_zip_py_rc_normalize(&rc)) return XX_ZIP_PY_XZ_MORE;
            *position = rc.position;
            return rc.code ? xx_zip_py_xz_fail(xz, xx_zip_py_xz_data) : XX_ZIP_PY_XZ_END;
        }
        /* far from both ends: many symbols at once (XX_ZIP_PY_XZ_UNKNOWN is
           the largest number, so it allows any amount) */
        bool decoded = false;
        if (xx_zip_py_lzma_fast_ready(lz, &rc, left)) {
            if (!xx_zip_py_xz_flush(xz)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_memory);
            size_t from = lz->at;
            decoded = xx_zip_py_lzma_fast(lz, &rc, left, &symbol);
            size_t produced = lz->at - from;
            if (produced && !xx_buf_append(xz->out, lz->dictionary + from, produced)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_memory);
            if (left != XX_ZIP_PY_XZ_UNKNOWN) left -= produced;
            if (!decoded) continue;
        }
        if (!decoded) xx_zip_py_lzma_decode(lz, &rc, &symbol);
        if (symbol.kind == XX_ZIP_PY_LZMA_MARKER && symbol.whole && left != XX_ZIP_PY_XZ_UNKNOWN)
            return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
        if (rc.starved) return XX_ZIP_PY_XZ_MORE;
        if (symbol.kind == XX_ZIP_PY_LZMA_BAD) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
        if (symbol.kind == XX_ZIP_PY_LZMA_MARKER) {
            *position = rc.position;
            return rc.code ? xx_zip_py_xz_fail(xz, xx_zip_py_xz_data) : XX_ZIP_PY_XZ_END;
        }
        if (symbol.kind == XX_ZIP_PY_LZMA_LITERAL) {
            lz->state = lz->state < 4 ? 0 : lz->state < 10 ? lz->state - 3 : lz->state - 6;
            if (!xx_zip_py_xz_put(xz, symbol.byte)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_memory);
            if (left != XX_ZIP_PY_XZ_UNKNOWN) --left;
            continue;
        }
        if (symbol.kind == XX_ZIP_PY_LZMA_MATCH) {
            lz->reps[3] = lz->reps[2];
            lz->reps[2] = lz->reps[1];
            lz->reps[1] = lz->reps[0];
            lz->reps[0] = symbol.distance;
            lz->state = lz->state < 7 ? 7 : 10;
            if (symbol.distance >= lz->total || symbol.distance >= lz->dictionary_size) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
        } else if (symbol.kind == XX_ZIP_PY_LZMA_SHORT_REP) {
            lz->state = lz->state < 7 ? 9 : 11;
            symbol.length = 1;
        } else {
            uint32_t distance = lz->reps[symbol.rep];
            for (unsigned i = symbol.rep; i; --i) lz->reps[i] = lz->reps[i - 1];
            lz->reps[0] = distance;
            lz->state = lz->state < 7 ? 8 : 11;
        }
        for (uint32_t i = 0; i < symbol.length; ++i) {
            /* a match that goes past the declared size */
            if (!left) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
            if (!xx_zip_py_xz_put(xz, xx_zip_py_lzma_back(lz, lz->reps[0]))) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_memory);
            if (left != XX_ZIP_PY_XZ_UNKNOWN) --left;
        }
    }
}

/* liblzma's LZMA2 decoder: the chunks from data[*position] up to the end
 * marker. The LZMA decoder of a chunk is given all the data there is, not
 * the chunk's bytes alone; taking more than the chunk declares is a fault,
 * as is taking less by the chunk's end. */
static xx_zip_py_xz_step xx_zip_py_xz_lzma2(xx_zip_py_xz *xz, const uint8_t *data, size_t size, size_t *position)
{
    xx_zip_py_lzma *lz = &xz->lz;
    bool need_properties = true, need_reset = true;
    size_t at = *position;
    for (;;) {
        if (at == size) return XX_ZIP_PY_XZ_MORE;
        unsigned control = data[at++];
        if (!control) {
            *position = at;
            return XX_ZIP_PY_XZ_END;
        }
        if (control >= 0xE0 || control == 1)
            need_properties = need_reset = true;
        else if (need_reset)
            return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
        if (control < 0x80 ? control > 2 : (control < 0xC0 && need_properties)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
        if (need_reset) {
            need_reset = false;
            lz->total = 0;
            lz->at = 0;
        }
        if (control < 0x80) {
            /* an uncompressed chunk */
            if (size - at < 2) return XX_ZIP_PY_XZ_MORE;
            size_t count = ((size_t)data[at] << 8 | data[at + 1]) + 1;
            at += 2;
            size_t have = count < size - at ? count : size - at;
            if (!xx_zip_py_xz_put_bytes(xz, data + at, have)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_memory);
            at += have;
            if (have != count) return XX_ZIP_PY_XZ_MORE;
            continue;
        }
        if (size - at < 4) return XX_ZIP_PY_XZ_MORE;
        uint64_t uncompressed = (((uint64_t)(control & 0x1F) << 16) | ((uint64_t)data[at] << 8) | data[at + 1]) + 1;
        size_t compressed = ((size_t)data[at + 2] << 8 | data[at + 3]) + 1;
        at += 4;
        if (control >= 0xC0) {
            if (at == size) return XX_ZIP_PY_XZ_MORE;
            unsigned properties = data[at++];
            if (properties > (4 * 5 + 4) * 9 + 8 || properties % 9 + (properties % 45) / 9 > 4)
                return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
            lz->pb = properties / 45;
            lz->lp = (properties % 45) / 9;
            lz->lc = properties % 9;
            need_properties = false;
            xx_zip_py_lzma_reset_state(lz);
        } else if (control >= 0xA0) {
            xx_zip_py_lzma_reset_state(lz);
        }
        size_t start = at;
        xx_zip_py_xz_step step = xx_zip_py_xz_lzma(xz, data, size, &at, uncompressed);
        if (step == XX_ZIP_PY_XZ_FAIL) return step;
        if ((step == XX_ZIP_PY_XZ_MORE ? size : at) - start > compressed) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
        if (step == XX_ZIP_PY_XZ_MORE) return step;
        if (at - start != compressed) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
    }
}

/* lzma_vli_decode() byte by byte: XX_ZIP_PY_XZ_MORE when the data ends
 * inside the number, XX_ZIP_PY_XZ_FAIL for a tenth byte or a last byte of 0. */
static xx_zip_py_xz_step xx_zip_py_xz_vli(const uint8_t *data, size_t end, size_t *position, uint64_t *value)
{
    uint64_t result = 0;
    for (unsigned i = 0; i < 9; ++i) {
        if (*position >= end) return XX_ZIP_PY_XZ_MORE;
        uint8_t byte = data[(*position)++];
        result |= (uint64_t)(byte & 0x7F) << (7 * i);
        if (!(byte & 0x80)) {
            if (i && byte == 0) return XX_ZIP_PY_XZ_FAIL; /* not minimally encoded */
            *value = result;
            return XX_ZIP_PY_XZ_END;
        }
    }
    return XX_ZIP_PY_XZ_FAIL;
}

static unsigned xx_zip_py_xz_vli_size(uint64_t value)
{
    unsigned count = 1;
    while (value >>= 7) ++count;
    return count;
}

static size_t xx_zip_py_xz_check_size(unsigned check)
{
    static const uint8_t sizes[16] = {0, 4, 4, 4, 8, 8, 8, 16, 16, 16, 32, 32, 32, 64, 64, 64};
    return sizes[check & 0x0F];
}

static bool xx_zip_py_xz_verify_check(unsigned check, const uint8_t *data, size_t size, const uint8_t *expected)
{
    switch (check) {
    case 0x01:
        return xx_crc32_calc(0, data, size) == xx_zip_py_rd32(expected);
    case 0x04:
        return xx_crc64_xz_calc(0, data, size) == xx_zip_py_rd64(expected);
    case 0x0A: {
        uint8_t digest[32];
        return xx_sha256_memory(data, size, digest) && memcmp(digest, expected, sizeof(digest)) == 0;
    }
    default:
        return true; /* liblzma skips check types it does not know */
    }
}

/* ---- the filters before LZMA2: liblzma's delta and BCJ decoders
 *
 * A BCJ filter keeps the bytes it cannot convert yet, so when the input ends
 * less has come out of the chain than LZMA2 gave; and with a declared
 * uncompressed size liblzma faults as soon as more than that has come out.
 * To count as liblzma counts, the chain below is liblzma's (simple_coder.c,
 * delta_decoder.c), run over the bytes LZMA2 gave with the output buffers
 * CPython gives liblzma. */

/* x86: the operand of CALL and JMP (E8, E9) from absolute to relative. */
static size_t xx_zip_py_xz_bcj_x86(xx_zip_py_xz_filter *f, uint8_t *buffer, size_t size)
{
    static const uint8_t allowed[8] = {1, 1, 1, 0, 1, 0, 0, 0}, bit_number[8] = {0, 1, 2, 2, 3, 3, 3, 3};
    uint32_t prev_mask = f->prev_mask, prev_pos = f->prev_pos, now_pos = f->now_pos;
    size_t at = 0;
    if (size < 5) return 0;
    if (now_pos - prev_pos > 5) prev_pos = now_pos - 5;
    const size_t limit = size - 5;
    while (at <= limit) {
        uint8_t b = buffer[at];
        if (b != 0xE8 && b != 0xE9) {
            ++at;
            continue;
        }
        const uint32_t offset = now_pos + (uint32_t)at - prev_pos;
        prev_pos = now_pos + (uint32_t)at;
        if (offset > 5) {
            prev_mask = 0;
        } else {
            for (uint32_t i = 0; i < offset; ++i) {
                prev_mask &= 0x77;
                prev_mask <<= 1;
            }
        }
        b = buffer[at + 4];
        if ((b == 0 || b == 0xFF) && allowed[(prev_mask >> 1) & 0x7] && (prev_mask >> 1) < 0x10) {
            uint32_t src = ((uint32_t)b << 24) | ((uint32_t)buffer[at + 3] << 16) | ((uint32_t)buffer[at + 2] << 8) | buffer[at + 1];
            uint32_t dest;
            for (;;) {
                dest = src - (now_pos + (uint32_t)at + 5);
                if (prev_mask == 0) break;
                const uint32_t i = bit_number[(prev_mask >> 1) & 0x7];
                b = (uint8_t)(dest >> (24 - i * 8));
                if (!(b == 0 || b == 0xFF)) break;
                src = dest ^ (((uint32_t)1 << (32 - i * 8)) - 1);
            }
            buffer[at + 4] = (uint8_t)(~(((dest >> 24) & 1) - 1));
            buffer[at + 3] = (uint8_t)(dest >> 16);
            buffer[at + 2] = (uint8_t)(dest >> 8);
            buffer[at + 1] = (uint8_t)dest;
            at += 5;
            prev_mask = 0;
        } else {
            ++at;
            prev_mask |= 1;
            if (b == 0 || b == 0xFF) prev_mask |= 0x10;
        }
    }
    f->prev_mask = prev_mask;
    f->prev_pos = prev_pos;
    return at;
}

/* PowerPC: bl (opcode 18 with AA = 0, LK = 1), big endian. */
static size_t xx_zip_py_xz_bcj_powerpc(uint32_t now_pos, uint8_t *buffer, size_t size)
{
    size_t i;
    for (i = 0; i + 4 <= size; i += 4) {
        if ((buffer[i] >> 2) == 0x12 && (buffer[i + 3] & 3) == 1) {
            const uint32_t src = (((uint32_t)buffer[i] & 3) << 24) | ((uint32_t)buffer[i + 1] << 16) | ((uint32_t)buffer[i + 2] << 8) |
                                 ((uint32_t)buffer[i + 3] & ~(uint32_t)3);
            const uint32_t dest = src - (now_pos + (uint32_t)i);
            buffer[i] = (uint8_t)(0x48 | ((dest >> 24) & 0x03));
            buffer[i + 1] = (uint8_t)(dest >> 16);
            buffer[i + 2] = (uint8_t)(dest >> 8);
            buffer[i + 3] = (uint8_t)((buffer[i + 3] & 0x03) | (uint8_t)dest);
        }
    }
    return i;
}

/* IA-64: the branch slots of a 16-byte instruction bundle. */
static size_t xx_zip_py_xz_bcj_ia64(uint32_t now_pos, uint8_t *buffer, size_t size)
{
    static const uint8_t branch_table[32] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                             4, 4, 6, 6, 0, 0, 7, 7, 4, 4, 0, 0, 4, 4, 0, 0};
    size_t i;
    for (i = 0; i + 16 <= size; i += 16) {
        const uint32_t mask = branch_table[buffer[i] & 0x1F];
        uint32_t bit_pos = 5;
        for (unsigned slot = 0; slot < 3; ++slot, bit_pos += 41) {
            if (((mask >> slot) & 1) == 0) continue;
            const size_t byte_pos = bit_pos >> 3;
            const uint32_t bit_res = bit_pos & 0x7;
            uint64_t instruction = 0;
            for (unsigned j = 0; j < 6; ++j) instruction += (uint64_t)buffer[i + j + byte_pos] << (8 * j);
            uint64_t inst_norm = instruction >> bit_res;
            if (((inst_norm >> 37) & 0xF) == 0x5 && ((inst_norm >> 9) & 0x7) == 0) {
                uint32_t src = (uint32_t)((inst_norm >> 13) & 0xFFFFF);
                src |= (uint32_t)((inst_norm >> 36) & 1) << 20;
                src <<= 4;
                uint32_t dest = src - (now_pos + (uint32_t)i);
                dest >>= 4;
                inst_norm &= ~((uint64_t)0x8FFFFF << 13);
                inst_norm |= (uint64_t)(dest & 0xFFFFF) << 13;
                inst_norm |= (uint64_t)(dest & 0x100000) << (36 - 20);
                instruction &= ((uint64_t)1 << bit_res) - 1;
                instruction |= inst_norm << bit_res;
                for (unsigned j = 0; j < 6; ++j) buffer[i + j + byte_pos] = (uint8_t)(instruction >> (8 * j));
            }
        }
    }
    return i;
}

/* ARM: BL (condition "always"), little endian. */
static size_t xx_zip_py_xz_bcj_arm(uint32_t now_pos, uint8_t *buffer, size_t size)
{
    size_t i;
    for (i = 0; i + 4 <= size; i += 4) {
        if (buffer[i + 3] == 0xEB) {
            uint32_t src = ((uint32_t)buffer[i + 2] << 16) | ((uint32_t)buffer[i + 1] << 8) | (uint32_t)buffer[i];
            src <<= 2;
            uint32_t dest = src - (now_pos + (uint32_t)i + 8);
            dest >>= 2;
            buffer[i + 2] = (uint8_t)(dest >> 16);
            buffer[i + 1] = (uint8_t)(dest >> 8);
            buffer[i] = (uint8_t)dest;
        }
    }
    return i;
}

/* ARM-Thumb: the two halves of BL. */
static size_t xx_zip_py_xz_bcj_armthumb(uint32_t now_pos, uint8_t *buffer, size_t size)
{
    size_t i;
    for (i = 0; i + 4 <= size; i += 2) {
        if ((buffer[i + 1] & 0xF8) == 0xF0 && (buffer[i + 3] & 0xF8) == 0xF8) {
            uint32_t src = (((uint32_t)buffer[i + 1] & 7) << 19) | ((uint32_t)buffer[i] << 11) | (((uint32_t)buffer[i + 3] & 7) << 8) |
                           (uint32_t)buffer[i + 2];
            src <<= 1;
            uint32_t dest = src - (now_pos + (uint32_t)i + 4);
            dest >>= 1;
            buffer[i + 1] = (uint8_t)(0xF0 | ((dest >> 19) & 0x7));
            buffer[i] = (uint8_t)(dest >> 11);
            buffer[i + 3] = (uint8_t)(0xF8 | ((dest >> 8) & 0x7));
            buffer[i + 2] = (uint8_t)dest;
            i += 2;
        }
    }
    return i;
}

/* SPARC: CALL, big endian. */
static size_t xx_zip_py_xz_bcj_sparc(uint32_t now_pos, uint8_t *buffer, size_t size)
{
    size_t i;
    for (i = 0; i + 4 <= size; i += 4) {
        if ((buffer[i] == 0x40 && (buffer[i + 1] & 0xC0) == 0x00) || (buffer[i] == 0x7F && (buffer[i + 1] & 0xC0) == 0xC0)) {
            uint32_t src = ((uint32_t)buffer[i] << 24) | ((uint32_t)buffer[i + 1] << 16) | ((uint32_t)buffer[i + 2] << 8) | (uint32_t)buffer[i + 3];
            src <<= 2;
            uint32_t dest = src - (now_pos + (uint32_t)i);
            dest >>= 2;
            dest = ((((uint32_t)0 - ((dest >> 22) & 1)) << 22) & 0x3FFFFFFF) | (dest & 0x3FFFFF) | 0x40000000;
            buffer[i] = (uint8_t)(dest >> 24);
            buffer[i + 1] = (uint8_t)(dest >> 16);
            buffer[i + 2] = (uint8_t)(dest >> 8);
            buffer[i + 3] = (uint8_t)dest;
        }
    }
    return i;
}

/* call_filter(): convert what can be converted of buffer[0 .. size); the
 * number of bytes done with. */
static size_t xx_zip_py_xz_bcj(xx_zip_py_xz_filter *f, uint8_t *buffer, size_t size)
{
    size_t done;
    switch (f->id) {
    case XX_ZIP_PY_XZ_FILTER_X86:
        done = xx_zip_py_xz_bcj_x86(f, buffer, size);
        break;
    case XX_ZIP_PY_XZ_FILTER_POWERPC:
        done = xx_zip_py_xz_bcj_powerpc(f->now_pos, buffer, size);
        break;
    case XX_ZIP_PY_XZ_FILTER_IA64:
        done = xx_zip_py_xz_bcj_ia64(f->now_pos, buffer, size);
        break;
    case XX_ZIP_PY_XZ_FILTER_ARM:
        done = xx_zip_py_xz_bcj_arm(f->now_pos, buffer, size);
        break;
    case XX_ZIP_PY_XZ_FILTER_ARMTHUMB:
        done = xx_zip_py_xz_bcj_armthumb(f->now_pos, buffer, size);
        break;
    default:
        done = xx_zip_py_xz_bcj_sparc(f->now_pos, buffer, size);
        break;
    }
    f->now_pos += (uint32_t)done;
    return done;
}

/* A filter of a block's chain as it starts: `offset` is a BCJ filter's
 * start offset, `distance` the delta filter's. */
static void xx_zip_py_xz_filter_start(xx_zip_py_xz_filter *f, unsigned id, uint32_t offset, unsigned distance)
{
    memset(f, 0, sizeof(*f));
    f->id = id;
    f->distance = distance;
    f->now_pos = offset;
    f->prev_pos = (uint32_t)0 - 5;
    /* twice the most a filter leaves unconverted */
    f->allocated = id == XX_ZIP_PY_XZ_FILTER_X86 ? 10 : id == XX_ZIP_PY_XZ_FILTER_IA64 ? 32 : 8;
}

/* lzma_bufcpy() from a filter's buffer. */
static void xx_zip_py_xz_filter_flush(xx_zip_py_xz_filter *f, uint8_t *out, size_t *out_pos, size_t out_size)
{
    size_t count = f->filtered - f->pos;
    if (count > out_size - *out_pos) count = out_size - *out_pos;
    if (count) memmove(out + *out_pos, f->buffer + f->pos, count);
    f->pos += count;
    *out_pos += count;
}

/* The code() function of the chain's coder number `level` (the last one is
 * LZMA2) with room at out[*out_pos .. out_size): true is LZMA_STREAM_END.
 * `stream` is the stream's output, where LZMA2's bytes lie from raw_at on:
 * a filter never has more bytes in its buffers and in the output than it
 * took from there, so out may be that same memory. */
static bool xx_zip_py_xz_chain_code(xx_zip_py_xz *xz, unsigned level, const uint8_t *stream, uint8_t *out, size_t *out_pos, size_t out_size)
{
    if (level == xz->filter_count) {
        /* LZMA2 fills the room it gets while it has input, and reports its
           end with the last byte */
        size_t count = xz->raw_end - xz->raw_at, room = out_size - *out_pos;
        if (count > room) count = room;
        if (count) memmove(out + *out_pos, stream + xz->raw_at, count);
        xz->raw_at += count;
        *out_pos += count;
        if (xz->raw_at == xz->raw_end && xz->raw_ended) return true;
        if (count < room) xz->raw_starved = true;
        return false;
    }
    xx_zip_py_xz_filter *f = &xz->filters[level];
    if (f->id == XX_ZIP_PY_XZ_FILTER_DELTA) {
        /* delta_decode(): each byte plus the byte `distance` before it */
        const size_t start = *out_pos;
        bool end = xx_zip_py_xz_chain_code(xz, level + 1, stream, out, out_pos, out_size);
        for (size_t i = start; i < *out_pos; ++i) {
            out[i] = (uint8_t)(out[i] + f->history[(f->distance + f->delta_pos) & 0xFF]);
            f->history[f->delta_pos--] = out[i];
        }
        return end;
    }

    /* simple_code() */
    if (f->pos < f->filtered) {
        xx_zip_py_xz_filter_flush(f, out, out_pos, out_size);
        if (f->pos < f->filtered) return false;
        if (f->end_was_reached) return true;
    }
    f->filtered = 0;
    {
        const size_t out_avail = out_size - *out_pos, buf_avail = f->size - f->pos;
        if (out_avail > buf_avail || buf_avail == 0) {
            /* the kept bytes go to the output, more is decoded behind them,
               and all of it is converted there */
            const size_t out_start = *out_pos;
            if (buf_avail) memmove(out + *out_pos, f->buffer + f->pos, buf_avail);
            *out_pos += buf_avail;
            if (xx_zip_py_xz_chain_code(xz, level + 1, stream, out, out_pos, out_size)) f->end_was_reached = true;
            const size_t size = *out_pos - out_start, unfiltered = size - xx_zip_py_xz_bcj(f, out + out_start, size);
            f->pos = 0;
            f->size = unfiltered;
            if (f->end_was_reached) {
                f->size = 0;
            } else if (unfiltered) {
                *out_pos -= unfiltered;
                memcpy(f->buffer, out + *out_pos, unfiltered);
            }
        } else if (f->pos > 0) {
            memmove(f->buffer, f->buffer + f->pos, buf_avail);
            f->size -= f->pos;
            f->pos = 0;
        }
    }
    if (f->size > 0) {
        /* the kept bytes stay in the buffer: fill it, convert, and give
           what was converted */
        if (xx_zip_py_xz_chain_code(xz, level + 1, stream, f->buffer, &f->size, f->allocated)) f->end_was_reached = true;
        f->filtered = xx_zip_py_xz_bcj(f, f->buffer, f->size);
        if (f->end_was_reached) f->filtered = f->size;
        xx_zip_py_xz_filter_flush(f, out, out_pos, out_size);
    }
    return f->end_was_reached && f->pos == f->size;
}

/* The next block of CPython's output buffer. */
static void xx_zip_py_xz_py_grow(xx_zip_py_xz *xz)
{
    uint64_t block = xx_zip_py_buffer_block(++xz->py_block);
    xz->py_end = block > SIZE_MAX - xz->py_end ? SIZE_MAX : xz->py_end + (size_t)block;
}

/* The bytes out[first ..] that LZMA2 gave for a block, through the block's
 * filters, in place, in the calls CPython makes: lzma_code() with the room
 * its output buffer has, a larger buffer when that is full, until liblzma
 * has taken all the input and leaves room. `ended`: LZMA2 reached its end;
 * otherwise the input ended first. Returns the number of bytes the chain has
 * given by then, which is all of them when LZMA2 ended. */
static size_t xx_zip_py_xz_chain_run(xx_zip_py_xz *xz, size_t first, bool ended)
{
    uint8_t *stream = (uint8_t *)xz->out->data + xz->stream_start;
    size_t start = first - xz->stream_start, position = start;
    xz->raw_at = start;
    xz->raw_end = xz->out->size - xz->stream_start;
    xz->raw_ended = ended;
    xz->raw_starved = false;
    for (unsigned idle = 0;;) {
        size_t before = position, raw_before = xz->raw_at;
        if (xx_zip_py_xz_chain_code(xz, 0, stream, stream, &position, xz->py_end)) break;
        if (position == xz->py_end) {
            xx_zip_py_xz_py_grow(xz);
        } else if (xz->raw_starved) {
            break;
        }
        /* liblzma gives up (LZMA_BUF_ERROR) after two calls that do nothing;
           a chain always does something while LZMA2 has bytes */
        idle = position == before && xz->raw_at == raw_before ? idle + 1 : 0;
        if (idle > 4) break;
    }
    return position - start;
}

typedef struct xx_zip_py_xz_record {
    uint64_t unpadded;
    uint64_t uncompressed;
} xx_zip_py_xz_record;

/* A block header of `header_size` bytes, all there, as
 * lzma_block_header_decode() and the start of the filter chain take it: the
 * filters one after the other (a number that is not one, an ID from 2^62 on
 * or properties longer than the header: data; an ID liblzma does not have
 * or properties of the wrong size or value: options), the padding, the
 * chain's rules (at most four filters come from the header's two bits;
 * LZMA2 last and only last), and then each filter's own start, where a BCJ
 * start offset that is not a multiple of the filter's alignment is refused.
 * The filters before LZMA2 are left in xz->filters. */
static xx_zip_py_xz_step xx_zip_py_xz_block_header(xx_zip_py_xz *xz, const uint8_t *header, size_t header_size, size_t check_size,
                                                   uint64_t *declared_compressed, uint64_t *declared_uncompressed, unsigned *lzma2)
{
    size_t end = header_size - 4, cursor = 2;
    uint8_t flags = header[1];
    unsigned filters = (flags & 0x03) + 1;
    bool chain = true, aligned = true;
    xz->filter_count = 0;
    if (xx_crc32_calc(0, header, end) != xx_zip_py_rd32(header + end)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
    if (flags & 0x3C) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
    *declared_compressed = *declared_uncompressed = XX_ZIP_PY_XZ_UNKNOWN;
    if (flags & 0x40) {
        /* a size of 0, or one that makes the block larger than a block can be */
        if (xx_zip_py_xz_vli(header, end, &cursor, declared_compressed) != XX_ZIP_PY_XZ_END || !*declared_compressed ||
            *declared_compressed > (XX_ZIP_PY_XZ_VLI_MAX & ~(uint64_t)3) - header_size - check_size)
            return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
    }
    if ((flags & 0x80) && xx_zip_py_xz_vli(header, end, &cursor, declared_uncompressed) != XX_ZIP_PY_XZ_END)
        return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
    for (unsigned i = 0; i < filters; ++i) {
        uint64_t id, size;
        if (xx_zip_py_xz_vli(header, end, &cursor, &id) != XX_ZIP_PY_XZ_END || id >= ((uint64_t)1 << 62) ||
            xx_zip_py_xz_vli(header, end, &cursor, &size) != XX_ZIP_PY_XZ_END || size > end - cursor)
            return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
        /* the filters liblzma (5.2, as CPython 3.13 has it) knows: LZMA2,
           which ends a chain, and delta and the six BCJ filters, which do
           not; ARM64 (0x0A) and RISC-V (0x0B) came later */
        if (id == XX_ZIP_PY_XZ_FILTER_LZMA2) {
            if (size != 1 || header[cursor] > 40) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
            *lzma2 = header[cursor];
            if (i + 1 != filters) chain = false;
        } else if (id == XX_ZIP_PY_XZ_FILTER_DELTA) {
            /* one byte: the distance less one */
            if (size != 1) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
            if (i + 1 == filters) chain = false;
            if (chain) xx_zip_py_xz_filter_start(&xz->filters[xz->filter_count++], (unsigned)id, 0, (unsigned)header[cursor] + 1);
        } else if (id >= XX_ZIP_PY_XZ_FILTER_X86 && id <= XX_ZIP_PY_XZ_FILTER_SPARC) {
            /* nothing, or the start offset in four bytes */
            static const uint8_t alignment[6] = {1, 4, 16, 4, 2, 4};
            if (size != 0 && size != 4) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
            uint32_t offset = size ? xx_zip_py_rd32(header + cursor) : 0;
            if (offset & (alignment[id - XX_ZIP_PY_XZ_FILTER_X86] - 1u)) aligned = false;
            if (i + 1 == filters) chain = false;
            if (chain) xx_zip_py_xz_filter_start(&xz->filters[xz->filter_count++], (unsigned)id, offset, 0);
        } else {
            return xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
        }
        cursor += (size_t)size;
    }
    for (; cursor < end; ++cursor)
        if (header[cursor]) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
    if (!chain || !aligned) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
    return XX_ZIP_PY_XZ_END;
}

/* One .xz stream from data[0], appended to xz->out: liblzma's stream
 * decoder. */
static xx_zip_py_xz_step xx_zip_py_xz_stream(xx_zip_py_xz *xz, const uint8_t *data, size_t size, size_t *used)
{
    xx_zip_py_xz_record *records = NULL;
    size_t record_count = 0, record_capacity = 0;
    xx_zip_py_xz_step step = XX_ZIP_PY_XZ_MORE;

    /* The stream header is looked at when all 12 bytes are there: the
       magic, its CRC-32, then the flags. */
    if (size < 12) return XX_ZIP_PY_XZ_MORE;
    if (memcmp(data, xx_zip_py_xz_magic, 6) != 0) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_format);
    if (xx_crc32_calc(0, data + 6, 2) != xx_zip_py_rd32(data + 8)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
    if (data[6] != 0 || (data[7] & 0xF0)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
    unsigned check = data[7] & 0x0F;
    size_t check_size = xx_zip_py_xz_check_size(check), position = 12;
    uint64_t blocks_size = 0, blocks_uncompressed = 0, blocks_list = 0;
    /* a decompressor of CPython's starts with an output buffer of one block */
    xz->stream_start = xz->out->size;
    xz->py_block = 0;
    xz->py_end = (size_t)xx_zip_py_buffer_block(0);

    for (;;) {
        if (position == size) goto done;
        if (data[position] == 0x00) break; /* index indicator */

        size_t header_size = ((size_t)data[position] + 1) * 4;
        uint64_t declared_compressed, declared_uncompressed;
        unsigned lzma2 = 0;
        if (size - position < header_size) goto done;
        step = xx_zip_py_xz_block_header(xz, data + position, header_size, check_size, &declared_compressed, &declared_uncompressed, &lzma2);
        if (step != XX_ZIP_PY_XZ_END) goto done;
        position += header_size;

        size_t start = position, first = xz->out->size;
        xx_zip_py_xz_lzma_start(xz, lzma2 == 40 ? UINT32_MAX : (uint64_t)(2 | (lzma2 & 1)) << (lzma2 / 2 + 11), 0);
        step = xx_zip_py_xz_lzma2(xz, data, size, &position);
        if (!xx_zip_py_xz_flush(xz)) step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_memory);
        if (step == XX_ZIP_PY_XZ_FAIL) goto done;
        /* The block decoder counts what the LZMA2 decoder took and what
           the chain gave: more than the header declares is a fault at once,
           less is one at the end of the data. Filters before LZMA2 hold
           bytes back, so the count is theirs. */
        uint64_t compressed = (step == XX_ZIP_PY_XZ_MORE ? size : position) - start, uncompressed = xz->out->size - first;
        if (xz->filter_count) {
            uncompressed = xx_zip_py_xz_chain_run(xz, first, step == XX_ZIP_PY_XZ_END);
        } else {
            /* the buffer grows when liblzma has filled it and has not ended the block */
            while (xz->out->size - xz->stream_start > xz->py_end) xx_zip_py_xz_py_grow(xz);
        }
        uint64_t limit = declared_compressed != XX_ZIP_PY_XZ_UNKNOWN ? declared_compressed
                                                                     : (XX_ZIP_PY_XZ_VLI_MAX & ~(uint64_t)3) - header_size - check_size;
        if (compressed > limit || (declared_uncompressed != XX_ZIP_PY_XZ_UNKNOWN && uncompressed > declared_uncompressed)) {
            step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
            goto done;
        }
        if (step == XX_ZIP_PY_XZ_MORE) goto done;
        if ((declared_compressed != XX_ZIP_PY_XZ_UNKNOWN && compressed != declared_compressed) ||
            (declared_uncompressed != XX_ZIP_PY_XZ_UNKNOWN && uncompressed != declared_uncompressed)) {
            step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
            goto done;
        }
        step = XX_ZIP_PY_XZ_MORE;
        while ((position - start) % 4) {
            if (position == size) goto done;
            if (data[position++]) {
                step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
                goto done;
            }
        }
        if (size - position < check_size) goto done;
        if (!xx_zip_py_xz_verify_check(check, (const uint8_t *)xz->out->data + first, xz->out->size - first, data + position)) {
            step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
            goto done;
        }
        position += check_size;
        if (record_count == record_capacity) {
            size_t larger = record_capacity ? record_capacity * 2 : 8;
            /* The byte count must not wrap (a 32-bit size_t): treated as memory that cannot be had. */
            xx_zip_py_xz_record *grown = larger > SIZE_MAX / sizeof(*records) || larger < record_capacity ? NULL :
                (xx_zip_py_xz_record *)xx_rt_realloc(records, larger * sizeof(*records));
            if (!grown) {
                step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_memory);
                goto done;
            }
            records = grown;
            record_capacity = larger;
        }
        records[record_count].unpadded = header_size + compressed + check_size;
        records[record_count].uncompressed = uncompressed;
        blocks_size += (records[record_count].unpadded + 3) & ~(uint64_t)3;
        blocks_uncompressed += uncompressed;
        blocks_list += xx_zip_py_xz_vli_size(records[record_count].unpadded) + xx_zip_py_xz_vli_size(uncompressed);
        ++record_count;
    }

    /* The index, as lzma_index_hash_decode() reads it: the count must be
       right when it is read; the records may not add up to more than the
       blocks while they are read, and must be the blocks' when all are
       read, the padding has passed and one more byte is there; the CRC-32
       is compared byte by byte. */
    size_t index_start = position++;
    uint64_t count = 0, records_size = 0, records_uncompressed = 0, records_list = 0;
    bool same = true;
    step = xx_zip_py_xz_vli(data, size, &position, &count);
    if (step == XX_ZIP_PY_XZ_END && count != record_count) step = XX_ZIP_PY_XZ_FAIL;
    for (size_t i = 0; step == XX_ZIP_PY_XZ_END && i < record_count; ++i) {
        uint64_t unpadded = 0, uncompressed = 0;
        step = xx_zip_py_xz_vli(data, size, &position, &unpadded);
        if (step == XX_ZIP_PY_XZ_END && (unpadded < 5 || unpadded > (XX_ZIP_PY_XZ_VLI_MAX & ~(uint64_t)3))) step = XX_ZIP_PY_XZ_FAIL;
        if (step == XX_ZIP_PY_XZ_END) step = xx_zip_py_xz_vli(data, size, &position, &uncompressed);
        if (step != XX_ZIP_PY_XZ_END) break;
        records_size += (unpadded + 3) & ~(uint64_t)3;
        records_uncompressed += uncompressed;
        records_list += xx_zip_py_xz_vli_size(unpadded) + xx_zip_py_xz_vli_size(uncompressed);
        if (unpadded != records[i].unpadded || uncompressed != records[i].uncompressed) same = false;
        if (blocks_size < records_size || blocks_uncompressed < records_uncompressed || blocks_list < records_list) step = XX_ZIP_PY_XZ_FAIL;
    }
    while (step == XX_ZIP_PY_XZ_END && (position - index_start) % 4) {
        if (position == size)
            step = XX_ZIP_PY_XZ_MORE;
        else if (data[position++])
            step = XX_ZIP_PY_XZ_FAIL;
    }
    /* liblzma compares when the next byte, the CRC-32's first, is there */
    if (step == XX_ZIP_PY_XZ_END && position == size) step = XX_ZIP_PY_XZ_MORE;
    if (step == XX_ZIP_PY_XZ_END && !same) step = XX_ZIP_PY_XZ_FAIL;
    if (step == XX_ZIP_PY_XZ_END) {
        uint32_t crc = xx_crc32_calc(0, data + index_start, position - index_start);
        for (unsigned i = 0; step == XX_ZIP_PY_XZ_END && i < 4; ++i, crc >>= 8) {
            if (position == size)
                step = XX_ZIP_PY_XZ_MORE;
            else if (data[position++] != (uint8_t)crc)
                step = XX_ZIP_PY_XZ_FAIL;
        }
    }
    if (step != XX_ZIP_PY_XZ_END) {
        if (step == XX_ZIP_PY_XZ_FAIL) xz->message = xx_zip_py_xz_data;
        goto done;
    }

    /* The stream footer, when all 12 bytes are there: the magic (a wrong
       one is a data error here, not a format error), its CRC-32, the flags,
       then what it says about the index and the header. */
    step = XX_ZIP_PY_XZ_MORE;
    if (size - position < 12) goto done;
    const uint8_t *footer = data + position;
    if (memcmp(footer + 10, "YZ", 2) != 0)
        step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
    else if (xx_crc32_calc(0, footer + 4, 6) != xx_zip_py_rd32(footer))
        step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
    else if (footer[8] != 0 || (footer[9] & 0xF0))
        step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_options);
    else if (((uint64_t)xx_zip_py_rd32(footer + 4) + 1) * 4 != (uint64_t)(position - index_start) || footer[9] != data[7])
        step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_data);
    else {
        *used = position + 12;
        step = XX_ZIP_PY_XZ_END;
    }
done:
    xx_rt_free(records);
    return step;
}

/* One .lzma (LZMA_Alone) stream from data[0], as liblzma's auto decoder
 * takes it: the properties, the dictionary size and the uncompressed size
 * are each judged when their last byte is there. */
static xx_zip_py_xz_step xx_zip_py_xz_alone(xx_zip_py_xz *xz, const uint8_t *data, size_t size, size_t *used)
{
    if (!size) return XX_ZIP_PY_XZ_MORE;
    unsigned props = data[0];
    if (props > (4 * 5 + 4) * 9 + 8 || props % 9 + (props % 45) / 9 > 4) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_format);
    if (size < 5) return XX_ZIP_PY_XZ_MORE;
    uint32_t dictionary = xx_zip_py_rd32(data + 1);
    if (dictionary != UINT32_MAX) {
        /* 2^n or 2^n + 2^(n-1) */
        uint32_t d = dictionary - 1;
        d |= d >> 2;
        d |= d >> 3;
        d |= d >> 4;
        d |= d >> 8;
        d |= d >> 16;
        ++d;
        if (d != dictionary) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_format);
    }
    if (size < 13) return XX_ZIP_PY_XZ_MORE;
    uint64_t declared = xx_zip_py_rd64(data + 5);
    if (declared != UINT64_MAX && declared >= ((uint64_t)1 << 38)) return xx_zip_py_xz_fail(xz, xx_zip_py_xz_format);

    size_t position = 13;
    xx_zip_py_xz_lzma_start(xz, dictionary, props);
    xx_zip_py_xz_step step = xx_zip_py_xz_lzma(xz, data, size, &position, declared);
    if (!xx_zip_py_xz_flush(xz)) step = xx_zip_py_xz_fail(xz, xx_zip_py_xz_memory);
    *used = position;
    return step;
}

bool xx_zip_py_xz_decompress(const uint8_t *data, size_t size, uint8_t **out, size_t *out_size, char **error)
{
    xx_buf_t result;
    xx_zip_py_xz *xz;
    size_t used = 0;
    const char *message = NULL;
    *out = NULL;
    *out_size = 0;
    if (error) *error = NULL;
    xx_buf_init(&result);
    xz = (xx_zip_py_xz *)xx_rt_malloc(sizeof(*xz));
    if (xz) {
        memset(xz, 0, offsetof(xx_zip_py_xz, pending));
        xz->out = &result;
        /* room for the literal coders of any lc + lp (at most 4) */
        xz->lz.literal = (uint16_t *)xx_rt_malloc(((size_t)0x300 << 4) * sizeof(uint16_t));
    }
    /* Allocated from the start: the checks read result.data even when no
       block produced output. */
    if (!xz || !xz->lz.literal || !xx_buf_reserve(&result, 0)) {
        message = xx_zip_py_xz_memory;
    } else {
        for (bool first = true;; first = false) {
            size_t before = result.size, taken = 0;
            /* FORMAT_AUTO: 0xFD starts an .xz stream, any other byte a
               .lzma one; without a byte the decoder waits. */
            xx_zip_py_xz_step step = used == size ? XX_ZIP_PY_XZ_MORE
                                                  : data[used] == xx_zip_py_xz_magic[0] ? xx_zip_py_xz_stream(xz, data + used, size - used, &taken)
                                                                                        : xx_zip_py_xz_alone(xz, data + used, size - used, &taken);
            if (step == XX_ZIP_PY_XZ_MORE) {
                message = xx_zip_py_xz_ended;
                break;
            }
            if (step == XX_ZIP_PY_XZ_FAIL) {
                /* what follows a decoded stream and is not a stream is ignored */
                if (!first) {
                    result.size = before;
                    result.data[before] = '\0';
                } else {
                    message = xz->message;
                }
                break;
            }
            used += taken;
            if (used == size) break;
        }
    }
    if (xz) {
        xx_rt_free(xz->lz.dictionary);
        xx_rt_free(xz->lz.literal);
        xx_rt_free(xz);
    }
    if (message) {
        xx_buf_free(&result);
        if (error && !*error) *error = xx_zip_py_strdup(message);
        return false;
    }
    char *bytes = xx_buf_detach(&result, out_size);
    if (!bytes) {
        *out_size = 0;
        if (error) *error = xx_zip_py_strdup(xx_zip_py_xz_memory);
        return false;
    }
    *out = (uint8_t *)bytes;
    return true;
}
