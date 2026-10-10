/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://vice-emu.sourceforge.io/vice_17.html
 * P64 version 0, CRC-checked HTP half-track/DONE chunks; exports original range-encoded flux streams, not pulse/audio decoding.
 */
#include "xxfclib/formats/commodore_p64/xx_commodore_p64.h"
#include "../common/xx_retro_disk_components.h"
#include "xxfclib/data/xx_data.h"

static bool parse_blob(Abstractformat *f, pm_stream *s, retro_disk_blob *b)
{
    uint32_t at = 24, end, n = 0;
    uint8_t seen[256] = {0};
    bool ok;
    char name[48];
    if (!retro_disk_range(b, 0, 24) || xx_rt_memcmp(b->p, "P64-1541", 8) || xx_data_get_u32(b->p + 8, 4, 0, false) || xx_data_get_u32(b->p + 12, 4, 0, false) > 3 ||
        !retro_disk_range(b, 24, xx_data_get_u32(b->p + 16, 4, 0, false)))
        return false;
    end = 24 + xx_data_get_u32(b->p + 16, 4, 0, false);
    if (retro_disk_crc(b, 24, end - 24, &ok) != xx_data_get_u32(b->p + 20, 4, 0, false) || !ok || !retro_disk_emit(f, s, b, "flux-descriptor.bin", 0, 24)) return false;
    while (at < end) {
        uint32_t z;
        uint8_t id;
        if (end - at < 12 || (z = xx_data_get_u32(b->p + at + 4, 4, 0, false)) > end - at - 12 ||
            retro_disk_crc(b, at + 12, z, &ok) != xx_data_get_u32(b->p + at + 8, 4, 0, false) || !ok)
            return false;
        if (!xx_rt_memcmp(b->p + at, "DONE", 4)) {
            if (z || at + 12 != end || !n) return false;
            if (!retro_disk_emit(f, s, b, "done.bin", at, 12)) return false;
            s->size = end;
            return true;
        }
        if (xx_rt_memcmp(b->p + at, "HTP", 3) || seen[id = b->p[at + 3]] || (id & 127U) < 2 || (id & 127U) > 85 || ((id & 128U) && !(b->p[12] & 2U)) || z < 8 ||
            xx_data_get_u32(b->p + at + 12, 4, 0, false) > 4000000U || xx_data_get_u32(b->p + at + 16, 4, 0, false) != z - 8 ||
            (xx_data_get_u32(b->p + at + 12, 4, 0, false) && z < 12))
            return false;
        seen[id] = 1;
        ++n;
        xx_rt_snprintf(name, sizeof(name), "halftrack-%u.flux", id);
        if (!retro_disk_emit(f, s, b, name, at, 12 + z)) return false;
        at += 12 + z;
    }
    return false;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    retro_disk_blob b;
    bool ok;
    if (!retro_disk_load(f, &b, pd)) return false;
    ok = parse_blob(f, s, &b);
    xx_mem_free(b.p);
    return ok;
}

void xx_commodore_p64_init(xx_commodore_p64 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_COMMODORE_P64, "p64");
    }
}
xx_commodore_p64 *xx_commodore_p64_create(xx_io_device *d, int64_t b)
{
    xx_commodore_p64 *r = (xx_commodore_p64 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_commodore_p64_init(r, d, b);
    return r;
}
void xx_commodore_p64_destroy(xx_commodore_p64 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_commodore_p64_free(xx_commodore_p64 *r)
{
    if (r) {
        xx_commodore_p64_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_commodore_p64_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_commodore_p64_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
