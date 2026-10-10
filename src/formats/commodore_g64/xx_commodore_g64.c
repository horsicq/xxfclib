/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://vice-emu.sourceforge.io/vice_17.html
 * G64 version 0, up to 84 half-tracks; exports bounded GCR tracks and optional speed maps. No GCR sector/filesystem decoding.
 */
#include "xxfclib/formats/commodore_g64/xx_commodore_g64.h"
#include "../common/xx_retro_disk_components.h"
#include "xxfclib/data/xx_data.h"

static bool parse_blob(Abstractformat *f, pm_stream *s, retro_disk_blob *b)
{
    uint32_t tracks, maxlen, base, i, end, nonempty = 0, count = 0;
    retro_disk_span spans[168];
    char name[48];
    if (!retro_disk_range(b, 0, 12) || xx_rt_memcmp(b->p, "GCR-1541", 8) || b->p[8] || !(tracks = b->p[9]) || tracks > 84 ||
        !(maxlen = xx_data_get_u16(b->p + 10, 2, 0, false)) || maxlen > 32768 || !retro_disk_range(b, 12, tracks * 8))
        return false;
    base = end = 12 + tracks * 8;
    if (!retro_disk_emit(f, s, b, "gcr-descriptor.bin", 0, base)) return false;
    for (i = 0; i < tracks; ++i) {
        uint32_t at = xx_data_get_u32(b->p + 12 + i * 4, 4, 0, false), speed = xx_data_get_u32(b->p + 12 + tracks * 4 + i * 4, 4, 0, false), len;
        if (!at) {
            if (speed > 3) return false;
            continue;
        }
        if (at < base || !retro_disk_range(b, at, 2) || !(len = xx_data_get_u16(b->p + at, 2, 0, false)) || len > maxlen || !retro_disk_range(b, at + 2, len) ||
            !retro_disk_disjoint(spans, &count, 168, at, len + 2))
            return false;
        if (end < at + 2 + len) {
            end = at + 2 + len;
        }
        ++nonempty;
        xx_rt_snprintf(name, sizeof(name), "halftrack-%u.gcr", i + 2);
        if (!retro_disk_emit(f, s, b, name, at + 2, len)) return false;
        if (speed > 3) {
            uint32_t z = (len + 3) / 4;
            if (speed < base || !retro_disk_range(b, speed, z) || !retro_disk_disjoint(spans, &count, 168, speed, z)) return false;
            if (end < speed + z) end = speed + z;
            xx_rt_snprintf(name, sizeof(name), "halftrack-%u.speed", i + 2);
            if (!retro_disk_emit(f, s, b, name, speed, z)) return false;
        }
    }
    if (!nonempty) {
        return false;
    }
    s->size = end;
    return true;
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

void xx_commodore_g64_init(xx_commodore_g64 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_COMMODORE_G64, "g64");
    }
}
xx_commodore_g64 *xx_commodore_g64_create(xx_io_device *d, int64_t b)
{
    xx_commodore_g64 *r = (xx_commodore_g64 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_commodore_g64_init(r, d, b);
    return r;
}
void xx_commodore_g64_destroy(xx_commodore_g64 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_commodore_g64_free(xx_commodore_g64 *r)
{
    if (r) {
        xx_commodore_g64_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_commodore_g64_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_commodore_g64_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
