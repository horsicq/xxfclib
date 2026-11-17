/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Commodore 64 self-dissolving archives (SDA): a C64 PRG whose BASIC line is
 * a single SYS call into the unpacker, followed by CBM ARC style records.
 *
 * Stub (little-endian): u16 load address 0x0401/0x0801/0x1C01; u16 link to
 * the next BASIC line; u16 line number 10/13/15/17; 9E 28 (SYS "(") then
 * either a short form (link = load + 0x0C: four ASCII digits "2063"/"7183"/
 * "1039", ')' at 0x0C, three zero bytes; line 10 also has A9 F7 85 FD at
 * 0x10) or a long form (link = load + 0x21: "7198", ')' at 0x0C, zero bytes
 * at 0x1C..0x1E). The records start at (line - 6) * 254 bytes, one byte less
 * for the 0x1C01/line-15 stub. A stub alone is not an archive.
 *
 * Record: u8 type (1 or 2), u8 method, u16 check value, u24 plaintext size,
 * u16 count of 254-byte sectors the record occupies, u8 CBM file type, u8
 * name length (<= 16), the PETSCII name, for type 2 three more bytes, and for
 * method 1 the RLE marker byte. Methods: 0 stored, 1 RLE, 2 Huffman, 3 LZW +
 * RLE, 4 Huffman + RLE, 5 LZW + RLE without a size (sector count 0; the LZW
 * end code and a 5-byte trailer close it). The next record starts at the
 * record start + sectors * 254. The walk ends at the first record that does
 * not validate (the zero padding after the last one); a record that validates
 * but fails to decode fails the archive. Each sized record's reads stay
 * inside its own sectors.
 *
 * RLE: the marker (0xFE unless method 1 names one) is followed by a count and
 * the byte to repeat; count 0 means 255 for type 1 and 256 for type 2 records.
 * Bits are taken LSB-first from each byte. LZW codes are assembled MSB-first
 * from 9..12 bits: 256 ends, 257 clears, 258 is the first free code, the width
 * grows when the next free code reaches (1 << width) - 1 and the table freezes
 * at 4096 entries. The Huffman table is 256 entries of a 5-bit length (<= 24,
 * value LSB-first) and the code of that length, LSB-first; codes are matched
 * one bit at a time by (length, value), the code value growing LSB-first.
 */
#include "xxfclib/formats/archive/c64_sda/xx_c64_sda.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
#define SDA_SECTOR 254U
#define SDA_UNSIZED_MAX (16U * 1024U * 1024U)
typedef struct sda_bits {
    const uint8_t *p;
    uint32_t n, pos, acc;
    unsigned cnt;
} sda_bits;
/* n bits, first bit read becomes the most significant one. */
static bool sda_get(sda_bits *b, unsigned n, uint32_t *v)
{
    unsigned i;
    uint32_t r = 0;
    while (b->cnt < n) {
        if (b->pos >= b->n) return false;
        b->acc |= (uint32_t)b->p[b->pos++] << b->cnt;
        b->cnt += 8U;
    }
    for (i = 0; i < n; ++i) {
        r = (r << 1U) | (b->acc & 1U);
        b->acc >>= 1U;
    }
    b->cnt -= n;
    *v = r;
    return true;
}
/* n bits, first bit read becomes bit 0. */
static bool sda_get_lsb(sda_bits *b, unsigned n, uint32_t *v)
{
    unsigned i;
    uint32_t r = 0, bit;
    for (i = 0; i < n; ++i) {
        if (!sda_get(b, 1, &bit)) return false;
        r |= bit << i;
    }
    *v = r;
    return true;
}
typedef struct sda_out {
    uint8_t *p;
    uint32_t n, cap, count;
    uint8_t marker, state;
    bool rle, type1;
} sda_out;
/* Feeds one decoded byte; *made receives how many output bytes it produced. */
static bool sda_feed(sda_out *o, uint8_t c, uint32_t *made)
{
    uint32_t k;
    *made = 0;
    if (o->rle) {
        if (o->state == 1U) {
            o->count = c ? c : (o->type1 ? 255U : 256U);
            o->state = 2;
            return true;
        }
        if (o->state == 0U && c == o->marker) {
            o->state = 1;
            return true;
        }
        if (o->state == 2U) {
            if (o->count > o->cap - o->n) return false;
            for (k = 0; k < o->count; ++k) o->p[o->n++] = c;
            o->state = 0;
            *made = o->count;
            return true;
        }
    }
    if (o->n >= o->cap) return false;
    o->p[o->n++] = c;
    *made = 1;
    return true;
}
static bool sda_put(sda_out *o, uint8_t c, bool sized, uint32_t size)
{
    uint32_t before = o->n, made;
    if (!sda_feed(o, c, &made)) return false;
    return !sized || made <= size - before;
}
static bool sda_lzw(ac_blob *ab, sda_bits *b, sda_out *o, bool sized, uint32_t size, bool trailer)
{
    uint16_t prefix[4096];
    uint8_t suffix[4096], stack[4100];
    uint32_t width = 9, next = 0x102, code, prev, c, v;
    uint8_t pfc;
    unsigned top;
    xx_mem_zero(prefix, sizeof(prefix));
    for (c = 0; c < 4096U; ++c) suffix[c] = (uint8_t)(c < 256U ? c : 0);
    /* The first code, and the first one after a clear, is a literal (or the
     * end code); a table code there has nothing to refer to. */
    if (!sda_get(b, 9, &code)) return false;
    if (code == 0x100U) goto finish;
    if (code > 0xFFU) return false;
    if (!sda_put(o, (uint8_t)code, sized, size)) return false;
    prev = code;
    pfc = (uint8_t)code;
    for (;;) {
        if (!ac_poll(ab)) return false;
        if (sized && o->n >= size) return o->state == 0;
        if (!sda_get(b, width, &code)) return false;
        if (code == 0x100U) goto finish;
        if (code == 0x101U) {
            width = 9;
            next = 0x102;
            xx_mem_zero(prefix, sizeof(prefix));
            for (c = 256; c < 4096U; ++c) suffix[c] = 0;
            if (!sda_get(b, 9, &c)) return false;
            if (c == 0x100U) goto finish;
            if (c > 0xFFU) return false;
            if (!sda_put(o, (uint8_t)c, sized, size)) return false;
            prev = c;
            pfc = (uint8_t)c;
            continue;
        }
        top = 0;
        c = code;
        if (c >= next) {
            stack[top++] = pfc;
            c = prev;
        }
        while (c > 0xFFU) {
            if (top >= 4096U || c >= 4096U) return false;
            stack[top++] = suffix[c];
            c = prefix[c];
        }
        stack[top++] = (uint8_t)c;
        pfc = (uint8_t)c;
        while (top)
            if (!sda_put(o, stack[--top], sized, size)) return false;
        if (next < 0x1000U) {
            prefix[next] = (uint16_t)(prev & 0xFFFU);
            suffix[next] = pfc;
            ++next;
            if (next == (1U << width) - 1U && width < 12U) ++width;
        }
        prev = code;
    }
finish:
    if (trailer && (!sda_get(b, 16, &v) || !sda_get(b, 24, &v))) return false;
    /* An end code before the declared size is reached is a damaged member. */
    return o->state == 0 && (!sized || o->n == size);
}
static bool sda_huffman(ac_blob *ab, sda_bits *b, sda_out *o, uint32_t size)
{
    uint8_t len[256];
    uint32_t code[256], sym, value, length, maxlen = 0, bit;
    for (sym = 0; sym < 256U; ++sym) {
        if (!sda_get_lsb(b, 5, &length) || length > 24U) return false;
        value = 0;
        if (length && !sda_get_lsb(b, length, &value)) return false;
        len[sym] = (uint8_t)length;
        code[sym] = value;
        if (length > maxlen) maxlen = length;
    }
    if (size < 1U) return true;
    length = 0;
    value = 0;
    while (o->n < size) {
        int found = -1, k;
        if ((o->n & 1023U) == 0 && !ac_poll(ab)) return false;
        if (!sda_get(b, 1, &bit)) return false;
        value |= bit << length;
        ++length;
        /* The last table entry with this (length, value) wins. */
        for (k = 255; k >= 0; --k)
            if (len[k] == length && code[k] == value) {
                found = k;
                break;
            }
        if (found >= 0) {
            if (!sda_put(o, (uint8_t)found, true, size)) return false;
            length = 0;
            value = 0;
        } else if (length >= 23U || length >= maxlen) return false;
    }
    return true;
}
static bool sda_detect(const uint8_t *p, uint32_t n, uint32_t *start)
{
    uint32_t load, line, link;
    if (n < 0x23U) return false;
    load = xx_data_get_u16(p, 2, 0, false);
    link = xx_data_get_u16(p + 2, 2, 0, false);
    line = xx_data_get_u16(p + 4, 2, 0, false);
    if ((load != 0x0401U && load != 0x0801U && load != 0x1C01U) || (line != 10U && line != 13U && line != 15U && line != 17U)) return false;
    if (p[6] != 0x9EU || p[7] != 0x28U || p[0x0C] != 0x29U) return false;
    if (link == load + 0x0CU) {
        if (xx_rt_memcmp(p + 8, "2063", 4) && xx_rt_memcmp(p + 8, "7183", 4) && xx_rt_memcmp(p + 8, "1039", 4)) return false;
        if (p[0x0D] || p[0x0E] || p[0x0F]) return false;
        if (line == 10U && xx_data_get_u32(p + 0x10, 4, 0, false) != UINT32_C(0xFD85F7A9)) return false;
    } else if (link == load + 0x21U) {
        if (xx_rt_memcmp(p + 8, "7198", 4) || p[0x1C] || p[0x1D] || p[0x1E]) return false;
    } else
        return false;
    *start = (line - 6U) * SDA_SECTOR - (load == 0x1C01U && link == 0x1C0DU && line == 15U ? 1U : 0U);
    return *start != n;
}
static void sda_name(char *out, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i) {
        uint8_t c = p[i];
        if (c >= 0xC1U && c <= 0xDAU) c = (uint8_t)(c - 0x80U);
        out[i] = (char)(c >= 0x20U && c < 0x7FU ? c : '_');
    }
    out[n] = 0;
    /* Names are flat: separators, ':' and "", ".", ".." never reach a path. */
    xx_archive_flat_name(out);
}
static bool sda_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    uint32_t pos;
    if (!sda_detect(b->p, b->n, &pos)) return false;
    while (pos <= b->n && b->n - pos >= 11U) {
        const uint8_t *r = b->p + pos;
        uint32_t type = r[0], method = r[1], size = (uint32_t)r[4] | (uint32_t)r[5] << 8U | (uint32_t)r[6] << 16U;
        uint32_t sectors = xx_data_get_u16(r + 7, 2, 0, false), name_n = r[10], at = pos + 11U + name_n, cap, lim;
        uint8_t marker = 0xFE;
        char name[24];
        sda_out o;
        sda_bits bits;
        bool ok;
        if (!ac_poll(b)) return false;
        if ((type != 1U && type != 2U) || method > 5U || (type == 1U && method > 2U) || name_n > 16U || sectors > 0x1025U || (!sectors && method != 5U)) break;
        /* A sized record occupies its sectors and no more (the last one may
         * be cut at the end of the file); every read below stays inside. */
        lim = sectors && (uint64_t)pos + (uint64_t)sectors * SDA_SECTOR < b->n ? pos + sectors * SDA_SECTOR : b->n;
        if (at > lim) break;
        sda_name(name, r + 11, name_n);
        if (type == 2U) {
            if (lim - at < 3U) break;
            at += 3U;
        }
        if (method == 1U) {
            if (at >= lim) break;
            marker = b->p[at++];
        }
        xx_mem_zero(&o, sizeof(o));
        cap = method == 5U ? SDA_UNSIZED_MAX : size;
        o.p = ac_alloc(b, cap);
        if (!o.p) return false;
        o.cap = cap;
        o.marker = marker;
        o.type1 = type == 1U;
        o.rle = method == 1U || method >= 3U;
        bits.p = b->p + at;
        bits.n = lim - at;
        bits.pos = 0;
        bits.acc = 0;
        bits.cnt = 0;
        if (method == 0U) {
            ok = size <= lim - at;
            if (ok) {
                xx_rt_memcpy(o.p, b->p + at, size);
                o.n = size;
            }
        } else if (method == 1U) {
            uint32_t i = at;
            ok = true;
            while (ok && o.n < size) {
                if (i >= lim) ok = false;
                else ok = sda_put(&o, b->p[i++], true, size);
            }
            ok = ok && o.state == 0;
        } else if (method == 2U || method == 4U)
            ok = sda_huffman(b, &bits, &o, size);
        else
            ok = sda_lzw(b, &bits, &o, method == 3U, size, method == 5U);
        if (ok && method == 5U) {
            uint32_t used = at + bits.pos - pos;
            sectors = (used + SDA_SECTOR - 1U) / SDA_SECTOR;
            ok = ac_compact(b, &o.p, cap, o.n);
        }
        /* A record that validated but does not decode is a damaged member,
         * not the end of the archive (the zero padding fails the header
         * checks above). */
        if (!ok) {
            ac_release(b, o.p, cap);
            return ac_error(b, "C64 SDA member does not decode");
        }
        if (!ac_memory(f, s, b, name, o.p, o.n, sectors * SDA_SECTOR, (uint16_t)method)) return false;
        xx_rt_snprintf(s->items[s->count - 1U].name, sizeof(s->items[0].name), "%s", name);
        if (sectors > (b->n - pos) / SDA_SECTOR + 1U) break;
        pos += sectors * SDA_SECTOR;
    }
    return s->count != 0;
}
AC_PARSE(sda_parse)
AC_DEFINE(c64_sda, XX_FILE_TYPE_C64_SDA, "sda")
