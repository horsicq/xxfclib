/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CPFF packages (".acp" files of a 1990s Windows setup builder).
 *
 *   0x00  8    "CPFFLZW "
 *   0x08       records, one per directory entry, in directory order:
 *              i32 plaintext size, i32 packed size, packed bytes
 *   EOF-4 i32  offset of the directory: i32 count, then per entry i32 name
 *              length (1..0x400) and the name (backslash separated path)
 *
 * Codec: LZW over MSB-first codes of 9..15 bits, bounded by the packed size.
 * 0x100 ends the stream (so does running out of packed bytes), 0x101 widens
 * the code by one bit, 0x102 resets the dictionary (width 9) and is followed
 * by a literal, 0x103 is the first free code. Before every code the width
 * grows by one when the next free code exceeds the largest code of the
 * current width (2^w - 1; 0x8000 at 15 bits). The dictionary holds 0x8000
 * entries; a code at or above the next free one repeats the previous string
 * plus its first byte. The decoded length must equal the plaintext size.
 */
#include "xxfclib/formats/archive/cpff/xx_cpff.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
#define CPFF_CODES 0x8000U
typedef struct cpff_bits {
    const uint8_t *p;
    uint32_t n, pos, acc;
    unsigned cnt;
} cpff_bits;
static bool cpff_get(cpff_bits *b, unsigned width, uint32_t *v)
{
    while (b->cnt < width) {
        if (b->pos >= b->n) return false;
        b->acc |= (uint32_t)b->p[b->pos++] << (24U - b->cnt);
        b->cnt += 8U;
    }
    *v = b->acc >> (32U - width);
    b->acc <<= width;
    b->cnt -= width;
    return true;
}
static bool cpff_decode(ac_blob *ab, const uint8_t *in, uint32_t packed, uint8_t *out, uint32_t size)
{
    uint16_t *prefix;
    uint8_t *suffix, *stack;
    uint32_t next = 0x103, width = 9, max = 0x1FF, code, prev, at = 0, top;
    uint8_t first;
    cpff_bits b;
    bool ok = false;
    prefix = (uint16_t *)ac_alloc(ab, CPFF_CODES * 2U);
    suffix = ac_alloc(ab, CPFF_CODES);
    stack = ac_alloc(ab, CPFF_CODES);
    if (!prefix || !suffix || !stack) goto done;
    xx_mem_zero(prefix, CPFF_CODES * 2U);
    for (code = 0; code < CPFF_CODES; ++code) suffix[code] = (uint8_t)code;
    b.p = in;
    b.n = packed;
    b.pos = 0;
    b.acc = 0;
    b.cnt = 0;
#define CPFF_NEXT(v) (((max < next && width < 15U) ? (++width, max = width == 15U ? 0x8000U : (1U << width) - 1U) : 0U), cpff_get(&b, width, &(v)))
    if (!CPFF_NEXT(code)) {
        ok = size == 0;
        goto done;
    }
    if (code > 0xFFU || at >= size) goto done;
    out[at++] = (uint8_t)code;
    prev = code;
    first = (uint8_t)code;
    for (;;) {
        if ((at & 0xFFFU) == 0 && !ac_poll(ab)) goto done;
        if (!CPFF_NEXT(code) || code == 0x100U) break;
        if (code == 0x101U) {
            if (width < 15U) {
                ++width;
                max = width == 15U ? 0x8000U : (1U << width) - 1U;
            }
            continue;
        }
        if (code == 0x102U) {
            next = 0x103;
            width = 9;
            max = 0x1FF;
            if (!CPFF_NEXT(code) || code == 0x100U) break;
            if (code > 0xFFU || at >= size) goto done;
            out[at++] = (uint8_t)code;
            prev = code;
            first = (uint8_t)code;
            continue;
        }
        {
            uint32_t c = code;
            top = 0;
            if (c >= next) {
                stack[top++] = first;
                c = prev;
            }
            while (c > 0xFFU) {
                if (top >= CPFF_CODES - 1U || c >= CPFF_CODES) goto done;
                stack[top++] = suffix[c];
                c = prefix[c];
            }
            stack[top++] = (uint8_t)c;
            first = (uint8_t)c;
            if (top > size - at) goto done;
            while (top) out[at++] = stack[--top];
            if (next < CPFF_CODES) {
                prefix[next] = (uint16_t)prev;
                suffix[next] = first;
                ++next;
            }
            prev = code;
        }
    }
#undef CPFF_NEXT
    ok = at == size;
done:
    ac_release(ab, (uint8_t *)prefix, CPFF_CODES * 2U);
    ac_release(ab, suffix, CPFF_CODES);
    ac_release(ab, stack, CPFF_CODES);
    return ok;
}
static bool cpff_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    uint32_t dir, count, at = 8, i, cursor;
    if (b->n < 20U || xx_rt_memcmp(b->p, "CPFFLZW ", 8)) return false;
    dir = xx_data_get_u32(b->p + b->n - 4U, 4, 0, false);
    if (dir < 8U || dir > b->n - 8U) return false;
    count = xx_data_get_u32(b->p + dir, 4, 0, false);
    if (!count || count > (b->n - dir - 4U) / 5U) return false;
    cursor = dir + 4U;
    for (i = 0; i < count; ++i) {
        uint32_t name_n, plain, packed, k;
        char name[1100];
        uint8_t *out;
        pm_member *m;
        if (!ac_poll(b) || b->n - 4U - cursor < 4U) return false;
        name_n = xx_data_get_u32(b->p + cursor, 4, 0, false);
        cursor += 4U;
        if (!name_n || name_n > 0x400U || name_n > b->n - 4U - cursor) return false;
        for (k = 0; k < name_n; ++k) {
            uint8_t c = b->p[cursor + k];
            if (c < 0x20U) return false;
            name[k] = (char)(c == '\\' ? '/' : (c >= 0x7FU ? '_' : c));
        }
        name[name_n] = 0;
        cursor += name_n;
        if (!xx_archive_relative_name_ok(name)) return ac_error(b, "CPFF member name is not a relative path");
        if (at > dir || dir - at < 8U) return false;
        plain = xx_data_get_u32(b->p + at, 4, 0, false);
        packed = xx_data_get_u32(b->p + at + 4U, 4, 0, false);
        if (plain > 0x7FFFFFFFU || packed > dir - at - 8U) return false;
        out = ac_alloc(b, plain);
        if (!out) return false;
        if (!cpff_decode(b, b->p + at + 8U, packed, out, plain)) {
            ac_release(b, out, plain);
            return ac_error(b, "CPFF member does not decode to its declared size");
        }
        if (!ac_memory(f, s, b, "member", out, plain, packed, 1)) return false;
        m = &s->items[s->count - 1U];
        m->display_name = xx_str_dup(name);
        if (!m->display_name) return false;
        xx_rt_snprintf(m->name, sizeof(m->name), "%s", name);
        at += 8U + packed;
    }
    return s->count != 0;
}
AC_PARSE(cpff_parse)
AC_DEFINE(cpff, XX_FILE_TYPE_CPFF, "acp")
