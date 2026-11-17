/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Standalone CreateInstall data packages (DISK1.PAK). Little-endian; d0, d1
 * and d2 are the i32 at 0, 4 and 8. Three header generations:
 *   1: d0 > 0, 0 < d1 < file size, u16 1 at 8, 0x0A and 0x0C, and the two
 *      string offsets i32 at 0x0E and 0x12 within 1..d1; fixed header 0x16.
 *   2: as 1 but i32 at 0x0E is 0, byte 0x12 is 0xFF and the string offsets
 *      sit at 0x13 and 0x17; fixed header 0x1B.
 *   3: d0 == file size, d1 > 0, 0 < d2 < file size, u16 0x0A0A at 0x33, i32 -1
 *      at 0x35, "xxx" at 0x39 and four i32 at 0x3C..0x48 within 0..d2-1;
 *      fixed header 0x4C.
 * A generation-1/2 header that fails its sub-test is not tried as 3.
 * The record stream starts after the fixed header plus the script area of d1
 * (generations 1/2) or d2 (3) bytes, and runs to an end record or the file
 * end. Record: type (u8; u16 in generation 3), u32 attributes, u64 FILETIME,
 * u16 name length. Type 4 ends, type 3 leaves the current folder (no name),
 * type 2 enters a folder, type 1 is a file: the name, an i32 plaintext size
 * and the compressed stream. Names are ANSI without terminator; a name of at
 * least three characters wrapped in double quotes loses the quotes. After a
 * file or folder record up to four zero bytes of encoder padding follow.
 * Files are classic LZARI (N 4096, F 60, threshold 2, 15-bit arithmetic
 * coder, spaces in the ring): the stream has no stored length, its end is
 * where the decoder stopped fetching bytes after the plaintext size, and the
 * model restarts for every member.
 */
#include "xxfclib/formats/archive/createinstall_pak/xx_createinstall_pak.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
#include "xxfclib/algo/ampk/xx_ampk.h"
#define CIP_DEPTH 64U
static int32_t cip_i32(const uint8_t *p)
{
    return (int32_t)xx_data_get_u32(p, 4, 0, false);
}
static uint16_t cip_u16(const uint8_t *p)
{
    return xx_data_get_u16(p, 2, 0, false);
}
static unsigned cip_variant(const uint8_t *h, uint32_t n)
{
    int32_t d0, d1, d2, k;
    if (n < 0x4CU) return 0;
    d0 = cip_i32(h);
    d1 = cip_i32(h + 4);
    d2 = cip_i32(h + 8);
    if (d0 >= 1 && d1 >= 1 && (uint32_t)d1 < n && cip_u16(h + 8) == 1U && cip_u16(h + 0x0A) == 1U && cip_u16(h + 0x0C) == 1U) {
        if (cip_i32(h + 0x0E) >= 1 && cip_i32(h + 0x0E) <= d1 && cip_i32(h + 0x12) >= 1 && cip_i32(h + 0x12) <= d1) return 1;
        if (cip_i32(h + 0x0E) == 0 && h[0x12] == 0xFFU && cip_i32(h + 0x13) >= 1 && cip_i32(h + 0x13) <= d1 && cip_i32(h + 0x17) >= 1 && cip_i32(h + 0x17) <= d1)
            return 2;
        return 0;
    }
    if ((uint32_t)d0 != n || d1 <= 0 || d2 <= 0 || (uint32_t)d2 >= n || cip_u16(h + 0x33) != 0x0A0AU || cip_i32(h + 0x35) != -1 || xx_rt_memcmp(h + 0x39, "xxx", 3))
        return 0;
    for (k = 0x3C; k <= 0x48; k += 4)
        if (cip_i32(h + k) < 0 || cip_i32(h + k) >= d2) return 0;
    return 3;
}
static bool cip_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    unsigned v = cip_variant(b->p, b->n), depth = 0;
    uint32_t at, rec, k;
    size_t lens[CIP_DEPTH];
    char path[1024];
    size_t path_n = 0;
    if (!v) return false;
    rec = v == 3U ? 16U : 15U;
    at = (v == 1U ? 0x16U : v == 2U ? 0x1BU : 0x4CU) + (uint32_t)cip_i32(b->p + (v == 3U ? 8 : 4));
    if (at > b->n) return false;
    path[0] = 0;
    while (at < b->n) {
        uint32_t type, name_n, i;
        const uint8_t *name;
        if (!ac_poll(b)) return false;
        if (b->n - at < rec) return false;
        type = v == 3U ? cip_u16(b->p + at) : b->p[at];
        name_n = cip_u16(b->p + at + rec - 2U);
        at += rec;
        if (type == 4U) break;
        if (type == 3U) {
            path_n = depth ? lens[--depth] : 0U;
            path[path_n] = 0;
            continue;
        }
        if ((type != 1U && type != 2U) || !name_n || name_n > b->n - at) return false;
        name = b->p + at;
        at += name_n;
        if (name_n >= 3U && name[0] == '"' && name[name_n - 1U] == '"') {
            ++name;
            name_n -= 2U;
        }
        if (type == 2U) {
            if (depth >= CIP_DEPTH || path_n + name_n + 2U >= sizeof(path)) return false;
            lens[depth++] = path_n;
            for (i = 0; i < name_n; ++i) {
                uint8_t c = name[i];
                path[path_n++] = (char)(c >= 0x20U && c < 0x7FU && c != '/' && c != 0x5CU && c != ':' ? c : '_');
            }
            /* A folder named "..", "." or ".. " never leaves the output folder. */
            path[path_n] = 0;
            xx_archive_flat_name(path + path_n - name_n);
            path[path_n++] = '/';
            path[path_n] = 0;
        } else {
            int32_t size;
            size_t written = 0, used = 0;
            uint8_t *out;
            char full[1300];
            size_t full_n = path_n;
            pm_member *m;
            if (b->n - at < 4U || (size = cip_i32(b->p + at)) < 0) return false;
            at += 4U;
            if (path_n + name_n + 1U >= sizeof(full)) return false;
            xx_rt_memcpy(full, path, path_n);
            for (i = 0; i < name_n; ++i) {
                uint8_t c = name[i];
                full[full_n++] = (char)(c >= 0x20U && c < 0x7FU && c != '/' && c != 0x5CU && c != ':' ? c : '_');
            }
            full[full_n] = 0;
            xx_archive_flat_name(full + path_n);
            out = ac_alloc(b, (uint32_t)size);
            if (!out) return false;
            if (!xx_ampk_lzari_decode_memory_ex(b->p + at, b->n - at, out, (size_t)size, &written, &used) || written != (size_t)size || used > b->n - at) {
                ac_release(b, out, (uint32_t)size);
                return ac_error(b, "CreateInstall member does not decode");
            }
            if (!ac_memory(f, s, b, "member", out, (uint32_t)size, (uint32_t)used, 1)) return false;
            m = &s->items[s->count - 1U];
            m->display_name = xx_str_dup(full);
            if (!m->display_name) return false;
            xx_rt_snprintf(m->name, sizeof(m->name), "%s", full);
            at += (uint32_t)used;
        }
        /* Encoder padding: at most four zero bytes before the next record. */
        for (k = 0; k < 4U && at < b->n && !b->p[at]; ++k) ++at;
    }
    return s->count != 0;
}
AC_PARSE(cip_parse)
AC_DEFINE(createinstall_pak, XX_FILE_TYPE_CREATEINSTALL_PAK, "pak")
