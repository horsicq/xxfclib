/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Self-extracting archives of the C64S emulator distribution (DOS MZ).
 *
 * Detection: "MZ", and either e_lfarlc (u32 at 0x18) == 0x1C with the u32
 * 0x64311959 (59 19 31 64) at 0x1C, or e_lfarlc == 0x3E with that u32 at
 * 0x42; the file is at least 0x800 bytes and a first record is found:
 *
 * Variant: version 2 when some i < 0x69C in the first 0x800 bytes has the
 * magic at i, 0xFF at i + 0x164 and 0x10A zero bytes at i + 0x5A; otherwise
 * version 1. The first record is the lowest offset i < 0x7E5 whose bytes pass
 * the variant's record check: a 13-byte NUL-padded 8.3 name (bytes 0x21..0x7F,
 * exactly one '.', a non-empty base and a 3-character extension, zero bytes
 * after the terminator), a valid non-zero DOS date/time and method 0 or 1.
 *   version 1 record, 0x16 bytes: i32 size, name[13], u8 method, u16 time,
 *     u16 date; size > 0 (stored: smaller than the file). There is no packed
 *     size: the next record follows the last byte the decoder consumed.
 *   version 2 record, 0x1C bytes: i32 size, i32 packed size, name[13], u8
 *     method, u16 zero, u16 time, u16 date; both sizes > 0, stored records
 *     have equal sizes; the next record is at the data start + packed size.
 * The walk stops at an all-zero or short header (the normal end) or one with
 * a negative size. No checksums.
 *
 * Method 0 is stored. Method 1 is LZW in independent chunks of 0x4000 output
 * bytes (the last one the remainder); every chunk restarts the decoder at the
 * next byte boundary. Bits are taken LSB-first from each byte and a code is
 * assembled LSB-first; codes 0..255 are literals and 256 is the first free
 * code (no clear or end code). The first code of a chunk is a 9-bit literal.
 * A code at or above the next free one repeats the previous string plus its
 * first byte. After each new entry the width grows when the next free code
 * reaches (1 << width) - 1; at 12 bits that instead restarts the dictionary
 * at width 9 with a new literal. A chunk must decode to exactly its size.
 */
#include "xxfclib/formats/archive/c64s/xx_c64s.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
#define C64S_MAGIC UINT32_C(0x64311959)
#define C64S_HEAD 0x800U
#define C64S_CHUNK 0x4000U
static uint32_t c64s_u32(const uint8_t *p)
{
    return xx_data_get_u32(p, 4, 0, false);
}
static uint16_t c64s_u16(const uint8_t *p)
{
    return xx_data_get_u16(p, 2, 0, false);
}
static bool c64s_name_ok(const uint8_t *p)
{
    unsigned i = 0, dots = 0, base = 0, ext = 0;
    for (; i < 13U && p[i]; ++i) {
        if (p[i] < 0x21U) return false;
        if (p[i] == '.') ++dots;
        else if (p[i] >= 0x80U) return false;
        else if (dots) ++ext;
        else ++base;
    }
    if (dots != 1U || !base || ext != 3U) return false;
    for (; i < 13U; ++i)
        if (p[i]) return false;
    return true;
}
static bool c64s_date_ok(uint32_t date, uint32_t time)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t month, day, year, limit;
    if (!date && !time) return false;
    if ((time >> 11) >= 24U || ((time >> 5) & 63U) >= 60U || (time & 31U) * 2U >= 60U) return false;
    month = (date >> 5) & 15U;
    day = date & 31U;
    if (month < 1U || month > 12U || day < 1U) return false;
    year = (date >> 9) + 1980U;
    limit = days[month - 1U] + (month == 2U && year % 4U == 0 && (year % 100U != 0 || year % 400U == 0) ? 1U : 0U);
    return day <= limit;
}
static bool c64s_record_ok(const uint8_t *h, bool v2, uint32_t size)
{
    if (!v2) {
        int32_t n = (int32_t)c64s_u32(h);
        uint8_t m = h[0x11];
        if (n <= 0 || m >= 2U || !c64s_date_ok(c64s_u16(h + 0x14), c64s_u16(h + 0x12)) || !c64s_name_ok(h + 4)) return false;
        return m != 0 || (uint32_t)n < size;
    } else {
        int32_t a = (int32_t)c64s_u32(h), b = (int32_t)c64s_u32(h + 4);
        uint8_t m = h[0x15];
        if (a <= 0 || b <= 0 || m >= 2U || c64s_u16(h + 0x16) || !c64s_date_ok(c64s_u16(h + 0x1A), c64s_u16(h + 0x18)) || !c64s_name_ok(h + 8)) return false;
        return m == 0 ? (uint32_t)a < size && a == b : (uint32_t)b < size;
    }
}
static bool c64s_find(const uint8_t *p, uint32_t n, bool *v2, uint32_t *start)
{
    uint32_t i, k;
    if (n < C64S_HEAD || p[0] != 'M' || p[1] != 'Z') return false;
    if (!((c64s_u32(p + 0x18) == 0x1CU && c64s_u32(p + 0x1C) == C64S_MAGIC) || (c64s_u32(p + 0x18) == 0x3EU && c64s_u32(p + 0x42) == C64S_MAGIC))) return false;
    *v2 = false;
    for (i = 0; i < C64S_HEAD - 0x164U && !*v2; ++i) {
        if (c64s_u32(p + i) != C64S_MAGIC || p[i + 0x164] != 0xFFU) continue;
        for (k = 0x5A; k < 0x164U && !p[i + k]; ++k) {
        }
        if (k == 0x164U) *v2 = true;
    }
    for (i = 0; i < C64S_HEAD - 0x1BU; ++i)
        if (c64s_record_ok(p + i, *v2, n)) {
            *start = i;
            return true;
        }
    return false;
}
typedef struct c64s_lzw {
    uint16_t prefix[4096];
    uint8_t suffix[4096], stack[4100];
} c64s_lzw;
/* One chunk of n output bytes from in[*pos...]; *pos advances to the byte
 * after the last one read. */
static bool c64s_chunk(c64s_lzw *t, const uint8_t *in, uint32_t avail, uint32_t *pos, uint8_t *out, uint32_t n)
{
    uint32_t at = 0, acc = 0, cnt = 0, p = *pos;
#define C64S_GET(w, v)                                         \
    do {                                                       \
        uint32_t k_, r_ = 0;                                   \
        for (k_ = 0; k_ < (w); ++k_) {                         \
            if (!cnt) {                                        \
                if (p >= avail) return false;                  \
                acc = in[p++];                                 \
                cnt = 8;                                       \
            }                                                  \
            r_ |= (acc & 1U) << k_;                            \
            acc >>= 1;                                         \
            --cnt;                                             \
        }                                                      \
        (v) = r_;                                              \
    } while (0)
    for (;;) {
        uint32_t next = 256, width = 9, code, old, c;
        uint8_t fin;
        bool reset = false;
        C64S_GET(9, c);
        if (c >= 256U || at >= n) return false;
        out[at++] = (uint8_t)c;
        old = c;
        fin = (uint8_t)c;
        if (at >= n) break;
        for (;;) {
            unsigned top = 0;
            C64S_GET(width, code);
            c = code;
            if (c >= next) {
                t->stack[top++] = fin;
                c = old;
            }
            while (c > 255U) {
                if (top >= 4096U) return false;
                t->stack[top++] = t->suffix[c];
                c = t->prefix[c];
            }
            t->stack[top++] = (uint8_t)c;
            fin = (uint8_t)c;
            if (top > n - at) return false;
            while (top) out[at++] = t->stack[--top];
            t->prefix[next] = (uint16_t)(old & 0xFFFU);
            t->suffix[next] = fin;
            ++next;
            if (next == (1U << width) - 1U) {
                if (width == 12U) reset = true;
                else ++width;
            }
            old = code;
            if (reset || at >= n) break;
        }
        if (at >= n) break;
    }
#undef C64S_GET
    *pos = p;
    return at == n;
}
static bool c64s_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    bool v2;
    uint32_t pos, head;
    c64s_lzw *t;
    if (!c64s_find(b->p, b->n, &v2, &pos)) return false;
    head = v2 ? 0x1CU : 0x16U;
    t = (c64s_lzw *)ac_alloc(b, sizeof(c64s_lzw));
    if (!t) return false;
    xx_mem_zero(t, sizeof(*t));
    while (pos <= b->n && b->n - pos >= head) {
        const uint8_t *h = b->p + pos;
        uint32_t i, size, packed = 0, data = pos + head, method, end;
        char name[14];
        uint8_t *out;
        bool zero = true;
        if (!ac_poll(b)) goto fail;
        for (i = 0; i < head; ++i)
            if (h[i]) zero = false;
        if (zero) break;
        size = c64s_u32(h);
        if (size > 0x7FFFFFFFU) break;
        if (v2) {
            packed = c64s_u32(h + 4);
            if (packed > 0x7FFFFFFFU || c64s_u16(h + 0x16)) break;
        }
        method = h[v2 ? 0x15 : 0x11];
        for (i = 0; i < 13U && h[(v2 ? 8U : 4U) + i]; ++i) {
            uint8_t c = h[(v2 ? 8U : 4U) + i];
            name[i] = (char)(c > 0x20U && c < 0x7FU && c != '/' && c != 0x5CU && c != ':' ? c : '_');
        }
        name[i] = 0;
        if (!i || method > 1U) goto fail;
        xx_archive_flat_name(name);
        /* A stored record holds exactly its plaintext: a smaller packed size
         * would copy bytes of the records after it. */
        if (v2 && method == 0U && size != packed) {
            ac_error(b, "C64S stored member sizes disagree");
            goto fail;
        }
        out = ac_alloc(b, size);
        if (!out) goto fail;
        if (method == 0U) {
            if (!ac_span(b, data, size)) {
                ac_release(b, out, size);
                goto fail;
            }
            if (size) xx_rt_memcpy(out, b->p + data, size);
            end = data + size;
        } else {
            uint32_t done = 0, p = data, avail = v2 ? (packed <= b->n - data ? data + packed : 0U) : b->n;
            if (!avail) {
                ac_release(b, out, size);
                goto fail;
            }
            while (done < size) {
                uint32_t k = size - done < C64S_CHUNK ? size - done : C64S_CHUNK;
                if (!ac_poll(b) || !c64s_chunk(t, b->p, avail, &p, out + done, k)) {
                    ac_release(b, out, size);
                    ac_error(b, "C64S member does not decode");
                    goto fail;
                }
                done += k;
            }
            end = p;
        }
        if (v2) {
            if (packed > b->n - data) {
                ac_release(b, out, size);
                goto fail;
            }
            end = data + packed;
        }
        if (!ac_memory(f, s, b, name, out, size, end - data, (uint16_t)method)) goto fail;
        xx_rt_snprintf(s->items[s->count - 1U].name, sizeof(s->items[0].name), "%s", name);
        pos = end;
    }
    ac_release(b, (uint8_t *)t, sizeof(c64s_lzw));
    return s->count != 0;
fail:
    ac_release(b, (uint8_t *)t, sizeof(c64s_lzw));
    return false;
}
AC_PARSE(c64s_parse)
AC_DEFINE(c64s, XX_FILE_TYPE_C64S, "exe")
