/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ChiefLZ "Multiple" archives, first directory version (the second version,
 * "ChfLZ_2", has its own reader in archive/chieflzmulti):
 *
 *   0x00  u8      0x0A, the length of the magic that follows
 *   0x01  10      04 0D "ChfLZ" 05 06 08
 *   0x13  u16     entry count (non-zero)
 *   0x15  count entries of 0x28 bytes:
 *         +0x00 u8  kind / attribute byte
 *         +0x0E u8  0 stored, otherwise ChiefLZ compressed
 *         +0x0F i32 packed size             +0x13 i32 unpacked size
 *         +0x17 u16 DOS time                +0x19 u16 DOS date
 *         +0x1B u8  name length (1..12)     +0x1C name
 *   member data follows the directory, each member advancing by its packed
 *   size.
 * Compressed members use byte-oriented LZSS: a flag byte, LSB first, 1 = one
 * literal byte, 0 = two bytes b0 b1 giving a 12-bit ring position
 * ((b1 >> 4) << 8 | b0) + 0x12 and a length (b1 & 15) + 3. The 4 KiB ring is
 * zero-filled and written from index 0. The packed size bounds the stream;
 * the decoded length must equal the unpacked size.
 */
#include "xxfclib/formats/archive/chieflz_v1/xx_chieflz_v1.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
#define CLZ1_ENTRY 0x28U
#define CLZ1_DATA 0x15U
static const uint8_t clz1_magic[11] = {0x0A, 0x04, 0x0D, 'C', 'h', 'f', 'L', 'Z', 0x05, 0x06, 0x08};
/* Version 1 archives in the wild hold one flat folder: the member name is the
 * Pascal string at +0x1B (the kind/parent words are not a path chain here). */
static bool clz1_path(const uint8_t *dir, uint32_t count, uint32_t index, char *out, size_t cap)
{
    const uint8_t *e = dir + index * CLZ1_ENTRY;
    uint32_t n = e[0x1B], k;
    (void)count;
    if (!n || n > 12U || n >= cap) return false;
    for (k = 0; k < n; ++k) {
        uint8_t c = e[0x1C + k];
        if (c < 0x20U || c == '/' || c == 0x5CU || c == ':') return false;
        out[k] = (char)(c < 0x7FU ? c : '_');
    }
    out[n] = 0;
    xx_archive_flat_name(out);
    return true;
}
static bool clz1_lzss(ac_blob *b, const uint8_t *in, uint32_t packed, uint8_t *out, uint32_t size)
{
    uint8_t ring[4096];
    uint32_t i = 0, at = 0, flags = 0, r = 0;
    xx_mem_zero(ring, sizeof(ring));
    while (i < packed) {
        flags >>= 1U;
        if (!(flags & 0x100U)) {
            flags = in[i++] | 0xFF00U;
            if (i >= packed) break;
        }
        if ((at & 0xFFFFU) == 0 && !ac_poll(b)) return false;
        if (flags & 1U) {
            if (at >= size) return false;
            out[at++] = ring[r] = in[i++];
            r = (r + 1U) & 0xFFFU;
        } else {
            uint32_t pos, len;
            if (packed - i < 2U) return false;
            pos = ((uint32_t)(in[i + 1U] >> 4) << 8 | in[i]) + 0x12U;
            len = (in[i + 1U] & 15U) + 3U;
            i += 2U;
            if (len > size - at) return false;
            while (len--) {
                uint8_t c = ring[pos & 0xFFFU];
                ++pos;
                out[at++] = ring[r] = c;
                r = (r + 1U) & 0xFFFU;
            }
        }
    }
    return at == size;
}
static bool clz1_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    uint32_t count, i, at;
    const uint8_t *dir;
    if (b->n < CLZ1_DATA + CLZ1_ENTRY || xx_rt_memcmp(b->p, clz1_magic, sizeof(clz1_magic))) return false;
    count = xx_data_get_u16(b->p + 0x13, 2, 0, false);
    if (!count || count > (b->n - CLZ1_DATA) / CLZ1_ENTRY) return false;
    dir = b->p + CLZ1_DATA;
    at = CLZ1_DATA + count * CLZ1_ENTRY;
    for (i = 0; i < count; ++i) {
        const uint8_t *e = dir + i * CLZ1_ENTRY;
        uint32_t packed = xx_data_get_u32(e + 0x0F, 4, 0, false), plain = xx_data_get_u32(e + 0x13, 4, 0, false);
        char path[300];
        uint8_t *out;
        pm_member *m;
        if (!ac_poll(b)) return false;
        if (packed > 0x7FFFFFFFU || plain > 0x7FFFFFFFU || !ac_span(b, at, packed) || !clz1_path(dir, count, i, path, sizeof(path))) return false;
        out = ac_alloc(b, plain);
        if (!out) return false;
        if (!e[0x0E]) {
            if (packed != plain) {
                ac_release(b, out, plain);
                return ac_error(b, "ChiefLZ stored member sizes disagree");
            }
            if (plain) xx_rt_memcpy(out, b->p + at, plain);
        } else {
            if (!clz1_lzss(b, b->p + at, packed, out, plain)) {
                ac_release(b, out, plain);
                return ac_error(b, "ChiefLZ member does not decode");
            }
        }
        if (!ac_memory(f, s, b, "member", out, plain, packed, (uint16_t)(e[0x0E] ? 4U : 2U))) return false;
        m = &s->items[s->count - 1U];
        m->display_name = xx_str_dup(path);
        if (!m->display_name) return false;
        xx_rt_snprintf(m->name, sizeof(m->name), "%s", path);
        at += packed;
    }
    return s->count != 0;
}
AC_PARSE(clz1_parse)
AC_DEFINE(chieflz_v1, XX_FILE_TYPE_CHIEFLZ_V1, "clz")
