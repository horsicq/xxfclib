/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Compaq diagnostics MSGDATA message containers: a directory of fixed-size
 * entries at offset 0, ended by an all-zero entry, followed by the stored
 * message blobs. Two entry layouts exist:
 *   34 bytes: char module[13], char file[13], u32 offset, u32 size
 *   32 bytes: char module[13], char file[13], u32 offset, u16 size
 * Names are NUL-terminated within their 13 bytes (byte 12 is always 0 and
 * byte 0 is not). The first entry's offset is the directory size (a multiple
 * of the entry size, inside the file) and the second entry starts where the
 * first one ends; exactly one of the two layouts must satisfy this.
 * Members are the stored slices, named by the file field.
 */
#include "xxfclib/formats/archive/compaq_msgdata/xx_compaq_msgdata.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
static bool md_name_ok(const uint8_t *p)
{
    return p[0] && !p[12];
}
static bool md_layout(const uint8_t *p, uint32_t n, bool wide)
{
    uint32_t entry = wide ? 0x22U : 0x20U, first, size0, size1, second;
    if (n < 0x44U) return false;
    first = xx_data_get_u32(p + 0x1A, 4, 0, false);
    size0 = wide ? xx_data_get_u32(p + 0x1E, 4, 0, false) : xx_data_get_u16(p + 0x1E, 2, 0, false);
    second = xx_data_get_u32(p + entry + 0x1A, 4, 0, false);
    size1 = wide ? xx_data_get_u32(p + entry + 0x1E, 4, 0, false) : xx_data_get_u16(p + entry + 0x1E, 2, 0, false);
    if (first > 0x7FFFFFFFU || second > 0x7FFFFFFFU || size0 > 0x7FFFFFFFU || size1 > 0x7FFFFFFFU) return false;
    return first && first % entry == 0 && first < n && second < n && size0 && size1 && second == first + size0 && md_name_ok(p) && md_name_ok(p + 0x0D) &&
        md_name_ok(p + entry) && md_name_ok(p + entry + 0x0D);
}
static bool md_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    bool wide = md_layout(b->p, b->n, true), narrow = md_layout(b->p, b->n, false);
    uint32_t entry = wide ? 0x22U : 0x20U, at = 0, dir_end;
    if (wide == narrow) return false;
    dir_end = xx_data_get_u32(b->p + 0x1A, 4, 0, false);
    for (;;) {
        uint32_t i, offset, size;
        char name[16];
        bool zero = true;
        if (!ac_poll(b)) return false;
        if (at > dir_end || dir_end - at < entry) return false;
        for (i = 0; i < entry; ++i)
            if (b->p[at + i]) zero = false;
        if (zero) break;
        offset = xx_data_get_u32(b->p + at + 0x1A, 4, 0, false);
        size = wide ? xx_data_get_u32(b->p + at + 0x1E, 4, 0, false) : xx_data_get_u16(b->p + at + 0x1E, 2, 0, false);
        if (size > 0x7FFFFFFFU || (size && (offset < dir_end || !ac_span(b, offset, size)))) return false;
        for (i = 0; i < 12U && b->p[at + 0x0D + i]; ++i) {
            uint8_t c = b->p[at + 0x0D + i];
            name[i] = (char)(c > 0x20U && c < 0x7FU ? c : '_');
        }
        name[i] = 0;
        if (!i) return false;
        xx_archive_flat_name(name);
        if (!ac_emit(f, s, b, name, size ? offset : 0U, size)) return false;
        xx_rt_snprintf(s->items[s->count - 1U].name, sizeof(s->items[0].name), "%s", name);
        at += entry;
    }
    return s->count != 0;
}
AC_PARSE(md_parse)
AC_DEFINE(compaq_msgdata, XX_FILE_TYPE_COMPAQ_MSGDATA, "dat")
