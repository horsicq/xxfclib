/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "CSid" resource archives of the game Chasm: The Rift (CSM.BIN):
 *   0x00  "CSid"
 *   0x04  u16 entry count (non-zero)
 *   0x06  count entries of 0x15 bytes:
 *         u8 name length (1..12), char name[12], u32 size, u32 absolute offset
 * Members are stored; every slice lies after the directory and inside the
 * file. The directory is read in place, so the (large) data is never loaded.
 */
#include "xxfclib/formats/archive/chasm_csid/xx_chasm_csid.h"
#include "../../xx_legacy_archive.h"
#include "../../xx_archive_output_name.h"
#define CSID_ENTRY 0x15U
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[6], *dir;
    uint32_t count, i, dir_end;
    int64_t total = pm_available(f);
    xx_legacy_archive_info *r = (xx_legacy_archive_info *)f;
    bool ok = false;
    r->number_of_records = 0;
    if (total < 6 + (int64_t)CSID_ENTRY || !pm_read(f, 0, h, 6) || xx_rt_memcmp(h, "CSid", 4)) return false;
    count = xx_data_get_u16(h + 4, 2, 0, false);
    dir_end = 6U + count * CSID_ENTRY;
    if (!count || (int64_t)dir_end > total) return false;
    dir = (uint8_t *)xx_mem_alloc(count * CSID_ENTRY);
    if (!dir) return false;
    if (!pm_read(f, 6, dir, count * CSID_ENTRY)) goto done;
    for (i = 0; i < count; ++i) {
        const uint8_t *e = dir + i * CSID_ENTRY;
        uint32_t name_n = e[0], size = xx_data_get_u32(e + 13, 4, 0, false), offset = xx_data_get_u32(e + 17, 4, 0, false), k;
        char name[16];
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!name_n || name_n > 12U || size > 0x7FFFFFFFU || offset > 0x7FFFFFFFU || offset < dir_end || (int64_t)offset + size > total) goto done;
        for (k = 0; k < name_n; ++k) {
            uint8_t c = e[1 + k];
            if (c < 0x20U) goto done;
            name[k] = (char)(c < 0x7FU && c != '/' && c != '\\' && c != ':' ? c : '_');
        }
        name[name_n] = 0;
        xx_archive_flat_name(name);
        if (!pm_add(f, s, name, offset, size)) goto done;
        xx_rt_snprintf(s->items[s->count - 1U].name, sizeof(s->items[0].name), "%s", name);
    }
    s->size = total;
    r->number_of_records = s->count;
    ok = true;
done:
    xx_mem_free(dir);
    return ok;
}
AC_DEFINE(chasm_csid, XX_FILE_TYPE_CHASM_CSID, "bin")
