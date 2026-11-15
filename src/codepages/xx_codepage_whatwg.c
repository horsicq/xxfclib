/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * WHATWG decoder state machines follow encoding_rs 0.8.42, copyright
 * Mozilla Foundation, MIT OR Apache-2.0. Tables additionally BSD-3-Clause.
 * See tools/codepages/LICENSE-encoding_rs for the complete notices.
 */
#include "xxfclib/codepages/xx_codepage.h"
#include "xx_codepage_whatwg_tables.h"

enum { W_UTF8 = 28, W_UTF16LE, W_UTF16BE, W_SJIS, W_EUCJP, W_EUCKR,
       W_GBK, W_GB18030, W_BIG5, W_ISO2022JP, W_REPLACEMENT, W_USER };

typedef struct whatwg_output {
    unsigned char *bytes;
    size_t capacity, length;
    int errors, full, overflow;
} whatwg_output;

static void whatwg_put(whatwg_output *o, uint32_t u)
{
    unsigned char b[4];
    size_t k, n;
    if (u <= 0x7f) { b[0] = (unsigned char)u; n = 1; }
    else if (u <= 0x7ff) { b[0] = (unsigned char)(0xc0 | (u >> 6)); b[1] = (unsigned char)(0x80 | (u & 63)); n = 2; }
    else if (u <= 0xffff) {
        b[0] = (unsigned char)(0xe0 | (u >> 12)); b[1] = (unsigned char)(0x80 | ((u >> 6) & 63));
        b[2] = (unsigned char)(0x80 | (u & 63)); n = 3;
    } else {
        b[0] = (unsigned char)(0xf0 | (u >> 18)); b[1] = (unsigned char)(0x80 | ((u >> 12) & 63));
        b[2] = (unsigned char)(0x80 | ((u >> 6) & 63)); b[3] = (unsigned char)(0x80 | (u & 63)); n = 4;
    }
    if (o->length > SIZE_MAX - n) { o->overflow = 1; o->length = SIZE_MAX; return; }
    if (o->bytes && !o->full) {
        if (o->length > o->capacity || n > o->capacity - o->length) o->full = 1;
        else for (k = 0; k < n; ++k) o->bytes[o->length + k] = b[k];
    }
    o->length += n;
}
static void whatwg_error(whatwg_output *o) { o->errors = 1; whatwg_put(o, 0xfffd); }
static int whatwg_space(unsigned char c) { return c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
static int whatwg_label_equal(const char *a, size_t n, const char *b)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)a[i];
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (!b[i] || c != (unsigned char)b[i]) return 0;
    }
    return b[n] == 0;
}
static int whatwg_encoding(const char *label)
{
    size_t i, n = 0;
    if (!label) return -1;
    while (*label && whatwg_space((unsigned char)*label)) ++label;
    while (label[n]) ++n;
    while (n && whatwg_space((unsigned char)label[n - 1])) --n;
    for (i = 0; i < sizeof(whatwg_labels) / sizeof(whatwg_labels[0]); ++i)
        if (whatwg_label_equal(label, n, whatwg_labels[i].name)) return (int)whatwg_labels[i].encoding;
    return -1;
}
static uint32_t whatwg_gb_scalar(uint32_t pointer)
{
    size_t lo = 0, hi = sizeof(whatwg_gb_ranges) / sizeof(whatwg_gb_ranges[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (pointer < whatwg_gb_ranges[mid].first) hi = mid;
        else if (pointer > whatwg_gb_ranges[mid].last) lo = mid + 1;
        else return whatwg_gb_ranges[mid].scalar + pointer - whatwg_gb_ranges[mid].first;
    }
    return 0;
}
static void whatwg_utf8(const unsigned char *s, size_t n, whatwg_output *o)
{
    size_t i = 0;
    while (i < n) {
        unsigned int b = s[i], count, j, low = 0x80, high = 0xbf;
        uint32_t u;
        if (b < 0x80) { whatwg_put(o, b); ++i; continue; }
        if (b >= 0xc2 && b <= 0xdf) { count = 2; u = b & 31; }
        else if (b >= 0xe0 && b <= 0xef) {
            count = 3; u = b & 15;
            if (b == 0xe0) low = 0xa0;
            if (b == 0xed) high = 0x9f;
        } else if (b >= 0xf0 && b <= 0xf4) {
            count = 4; u = b & 7;
            if (b == 0xf0) low = 0x90;
            if (b == 0xf4) high = 0x8f;
        } else { whatwg_error(o); ++i; continue; }
        for (j = 1; j < count; ++j) {
            if (j >= n - i || s[i + j] < low || s[i + j] > high) break;
            u = (u << 6) | (s[i + j] & 63); low = 0x80; high = 0xbf;
        }
        if (j == count) whatwg_put(o, u); else whatwg_error(o);
        i += j;
    }
}
static uint32_t whatwg_unit(const unsigned char *s, int be)
{
    return be ? ((uint32_t)s[0] << 8) | s[1] : ((uint32_t)s[1] << 8) | s[0];
}
static void whatwg_utf16(const unsigned char *s, size_t n, int be, whatwg_output *o)
{
    size_t i = 0;
    while (i < n) {
        uint32_t u, v;
        if (n - i < 2) { whatwg_error(o); break; }
        u = whatwg_unit(s + i, be); i += 2;
        if (u >= 0xd800 && u <= 0xdbff) {
            if (n - i < 2) { whatwg_error(o); break; }
            v = whatwg_unit(s + i, be);
            if (v >= 0xdc00 && v <= 0xdfff) { whatwg_put(o, 0x10000 + ((u - 0xd800) << 10) + v - 0xdc00); i += 2; }
            else whatwg_error(o);
        } else if (u >= 0xdc00 && u <= 0xdfff) whatwg_error(o);
        else whatwg_put(o, u);
    }
}
static void whatwg_eucjp(const unsigned char *s, size_t n, whatwg_output *o)
{
    size_t i = 0;
    while (i < n) {
        unsigned int a = s[i++], b, c;
        uint32_t u = 0;
        if (a < 0x80) { whatwg_put(o, a); continue; }
        if (a != 0x8e && a != 0x8f && (a < 0xa1 || a > 0xfe)) { whatwg_error(o); continue; }
        if (i == n) { whatwg_error(o); break; }
        b = s[i];
        if (a == 0x8e) {
            if (b >= 0xa1 && b <= 0xdf) { ++i; whatwg_put(o, 0xff61 + b - 0xa1); }
            else { if (b >= 0x80) ++i; whatwg_error(o); }
            continue;
        }
        if (b < 0xa1 || b > 0xfe) { if (b >= 0x80) ++i; whatwg_error(o); continue; }
        ++i;
        if (a == 0x8f) {
            if (i == n) { whatwg_error(o); break; }
            c = s[i];
            if (c < 0xa1 || c > 0xfe) { if (c >= 0x80) ++i; whatwg_error(o); continue; }
            ++i; u = whatwg_jis0212[(b - 0xa1) * 94 + c - 0xa1];
        } else u = whatwg_jis0208[(a - 0xa1) * 94 + b - 0xa1];
        if (u) whatwg_put(o, u); else whatwg_error(o);
    }
}
static void whatwg_dbcs(int enc, const unsigned char *s, size_t n, whatwg_output *o)
{
    size_t i = 0;
    while (i < n) {
        unsigned int a = s[i++], b, lead;
        uint32_t u = 0, pointer = 0;
        if (a < 0x80) { whatwg_put(o, a); continue; }
        if (enc == W_SJIS) {
            if (a == 0x80) { whatwg_put(o, a); continue; }
            if (a >= 0xa1 && a <= 0xdf) { whatwg_put(o, 0xff61 + a - 0xa1); continue; }
            if ((a < 0x81 || a > 0x9f) && (a < 0xe0 || a > 0xfc)) { whatwg_error(o); continue; }
        } else if ((enc == W_GBK || enc == W_GB18030) && a == 0x80) { whatwg_put(o, 0x20ac); continue; }
        else if (a < 0x81 || a > 0xfe) { whatwg_error(o); continue; }
        if (i == n) { whatwg_error(o); break; }
        b = s[i];
        if (enc == W_GBK || enc == W_GB18030) {
            /* GBK's decoder intentionally accepts the same four-byte forms. */
            if (b >= 0x30 && b <= 0x39) {
                if (n - i < 2) { whatwg_error(o); break; }
                if (s[i + 1] < 0x81 || s[i + 1] > 0xfe) { whatwg_error(o); continue; }
                if (n - i < 3) { whatwg_error(o); break; }
                if (s[i + 2] < 0x30 || s[i + 2] > 0x39) { whatwg_error(o); continue; }
                pointer = ((a - 0x81) * 10 + b - 0x30) * 1260 + (s[i + 1] - 0x81) * 10 + s[i + 2] - 0x30;
                i += 3; u = whatwg_gb_scalar(pointer);
                if (u) whatwg_put(o, u); else whatwg_error(o);
                continue;
            }
            if (b >= 0x40 && b <= 0xfe && b != 0x7f)
                u = whatwg_gbk[(a - 0x81) * 190 + b - (b < 0x7f ? 0x40 : 0x41)];
        } else if (enc == W_SJIS) {
            lead = a - (a < 0xa0 ? 0x81 : 0xc1);
            if (b >= 0x40 && b <= 0xfc && b != 0x7f)
                u = whatwg_shift_jis[lead * 188 + b - (b < 0x7f ? 0x40 : 0x41)];
        } else if (enc == W_EUCKR) {
            if (b >= 0x41 && b <= 0xfe) u = whatwg_euc_kr[(a - 0x81) * 190 + b - 0x41];
        } else {
            if ((b >= 0x40 && b <= 0x7e) || (b >= 0xa1 && b <= 0xfe)) {
                pointer = (a - 0x81) * 157 + b - (b < 0x7f ? 0x40 : 0x62);
                u = whatwg_big5[pointer];
                if (pointer == 1133 || pointer == 1135 || pointer == 1164 || pointer == 1166) {
                    ++i; whatwg_put(o, pointer < 1164 ? 0xca : 0xea);
                    whatwg_put(o, pointer == 1133 || pointer == 1164 ? 0x304 : 0x30c); continue;
                }
            }
        }
        if (u) { ++i; whatwg_put(o, u); }
        else { if (b >= 0x80) ++i; whatwg_error(o); }
    }
}
static void whatwg_iso2022jp(const unsigned char *s, size_t n, whatwg_output *o)
{
    /* Output modes: ASCII, Roman, Katakana, JIS0208. The local pending lead
     * makes the WHATWG trail state explicit; escapes return to output mode. */
    unsigned int mode = 0, lead = 0;
    int output_flag = 0;
    size_t i = 0;
    while (i < n) {
        unsigned int b = s[i], next_mode = 4;
        if (b == 0x1b) {
            if (lead) { whatwg_error(o); lead = 0; }
            if (n - i >= 3) {
                if (s[i + 1] == 0x28) {
                    if (s[i + 2] == 0x42) next_mode = 0;
                    else if (s[i + 2] == 0x4a) next_mode = 1;
                    else if (s[i + 2] == 0x49) next_mode = 2;
                } else if (s[i + 1] == 0x24 && (s[i + 2] == 0x40 || s[i + 2] == 0x42)) next_mode = 3;
            }
            if (next_mode != 4) {
                mode = next_mode; i += 3;
                if (output_flag) whatwg_error(o);
                output_flag = 1;
            } else { ++i; output_flag = 0; whatwg_error(o); }
            continue;
        }
        ++i; output_flag = 0;
        if (lead) {
            uint32_t u = b >= 0x21 && b <= 0x7e ? whatwg_jis0208[(lead - 0x21) * 94 + b - 0x21] : 0;
            lead = 0;
            if (u) whatwg_put(o, u); else whatwg_error(o);
        } else if (mode <= 1) {
            if (b > 0x7f || b == 0x0e || b == 0x0f) whatwg_error(o);
            else if (mode == 1 && b == 0x5c) whatwg_put(o, 0xa5);
            else if (mode == 1 && b == 0x7e) whatwg_put(o, 0x203e);
            else whatwg_put(o, b);
        } else if (mode == 2) {
            if (b >= 0x21 && b <= 0x5f) whatwg_put(o, 0xff61 + b - 0x21); else whatwg_error(o);
        } else if (b >= 0x21 && b <= 0x7e) lead = b;
        else whatwg_error(o);
    }
    if (lead) whatwg_error(o);
}

xx_codepage_status xx_codepage_decode_whatwg(const char *label, const unsigned char *s, size_t n,
                                           unsigned char *out, size_t capacity, size_t *written, int *had_errors)
{
    int enc;
    size_t i;
    whatwg_output o = { out, capacity, 0, 0, 0, 0 };
    if (written) *written = 0;
    if (had_errors) *had_errors = 0;
    if (!written || (!s && n) || (!out && capacity)) return XX_CODEPAGE_INVALID;
    enc = whatwg_encoding(label);
    if (enc < 0) return XX_CODEPAGE_UNMAPPABLE;
    if (enc < W_UTF8) {
        for (i = 0; i < n; ++i) {
            uint32_t u = s[i] < 0x80 ? s[i] : whatwg_single_byte[enc][s[i] - 0x80];
            if (u & 0x80000000u) { o.errors = 1; u &= 0x7fffffffu; }
            whatwg_put(&o, u);
        }
    } else if (enc == W_UTF8) whatwg_utf8(s, n, &o);
    else if (enc == W_UTF16LE || enc == W_UTF16BE) whatwg_utf16(s, n, enc == W_UTF16BE, &o);
    else if (enc == W_EUCJP) whatwg_eucjp(s, n, &o);
    else if (enc == W_ISO2022JP) whatwg_iso2022jp(s, n, &o);
    else if (enc == W_REPLACEMENT) { if (n) whatwg_error(&o); }
    else if (enc == W_USER) { for (i = 0; i < n; ++i) whatwg_put(&o, s[i] < 0x80 ? s[i] : 0xf780 + s[i] - 0x80); }
    else whatwg_dbcs(enc, s, n, &o);
    *written = o.length;
    if (had_errors) *had_errors = o.errors;
    return o.full || o.overflow ? XX_CODEPAGE_BUFFER_TOO_SMALL : XX_CODEPAGE_OK;
}
