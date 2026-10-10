/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ValveSoftware/halflife/master/engine/studio.h
 * GoldSrc studio model IDST version10 texture-only companions, up to100 indexed textures and bounded skin families. Validates texture/name/palette/skin-table ranges and
 * indices. Exports indexed planes, RGB palettes and skin mappings; geometry/animation/sequence groups, external textures and rendering unsupported.
 */
#include "xxfclib/formats/valve_studio_mdl/xx_valve_studio_mdl.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static uint32_t g32(const uint8_t *p, bool be)
{
    return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false);
}
static bool span(uint64_t at, uint64_t n, uint64_t total)
{
    return at <= total && n <= total - at;
}
static bool overlap(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool emit(Abstractformat *f, pm_stream *s, const char *name, uint64_t at, uint64_t n, uint64_t total)
{
    size_t i;
    if (!span(at, n, total) || total > (uint64_t)pm_available(f) || s->count >= 4096) return false;
    for (i = 0; i < s->count; ++i)
        if (overlap(at, n, (uint64_t)(s->items[i].offset - f->base_address), (uint64_t)s->items[i].size)) return false;
    return pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static bool finite32(const uint8_t *p, bool be)
{
    return (g32(p, be) & 0x7f800000U) != 0x7f800000U;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[244], e[80], p[2];
    uint32_t total, count, table, skinrefs, families, skins, i, w, height, data;
    uint64_t metadata_end, skinbytes;
    char label[40];
    if (!pm_read(f, 0, h, 244) || xx_rt_memcmp(h, "IDST", 4) || xx_data_get_u32(h + 4, 4, 0, false) != 10 || !xx_rt_memchr(h + 8, 0, 64)) {
        return false;
    }
    total = xx_data_get_u32(h + 72, 4, 0, false);
    if (total < 244 || total > (uint64_t)pm_available(f)) {
        return false;
    }
    for (i = 76; i < 136; i += 4)
        if (!finite32(h + i, false)) return false;
    for (i = 140; i < 180; i += 4) {
        if (xx_data_get_u32(h + i, 4, 0, false)) return false;
    }
    for (i = 204; i < 244; i += 4)
        if (xx_data_get_u32(h + i, 4, 0, false)) return false;
    count = xx_data_get_u32(h + 180, 4, 0, false);
    table = xx_data_get_u32(h + 184, 4, 0, false);
    skinrefs = xx_data_get_u32(h + 192, 4, 0, false);
    families = xx_data_get_u32(h + 196, 4, 0, false);
    skins = xx_data_get_u32(h + 200, 4, 0, false);
    skinbytes = (uint64_t)skinrefs * families * 2;
    if (!count || count > 100 || table < 244 || !span(table, (uint64_t)count * 80, total) || skinrefs > 100 || families > 100 || (!!skinrefs != !!families) ||
        (skinbytes && (skins < 244 || !span(skins, skinbytes, total) || overlap(table, (uint64_t)count * 80, skins, skinbytes))))
        return false;
    metadata_end = (uint64_t)table + count * 80;
    if (skinbytes && skins + skinbytes > metadata_end) metadata_end = skins + skinbytes;
    if (xx_data_get_u32(h + 188, 4, 0, false) && (xx_data_get_u32(h + 188, 4, 0, false) < metadata_end || xx_data_get_u32(h + 188, 4, 0, false) > total)) return false;
    for (i = 0; i < count; ++i) {
        if (stop(pd) || !pm_read(f, (int64_t)table + i * 80, e, 80) || !xx_rt_memchr(e, 0, 64) || (xx_data_get_u32(e + 64, 4, 0, false) & ~127U)) return false;
        w = xx_data_get_u32(e + 68, 4, 0, false);
        height = xx_data_get_u32(e + 72, 4, 0, false);
        data = xx_data_get_u32(e + 76, 4, 0, false);
        if (!w || w > 4096 || !height || height > 4096 || data < metadata_end || !span(data, (uint64_t)w * height + 768, total)) return false;
        xx_rt_snprintf(label, sizeof(label), "texture-%u.indices", i);
        if (!emit(f, s, label, data, (uint64_t)w * height, total)) return false;
        xx_rt_snprintf(label, sizeof(label), "texture-%u.rgb-palette", i);
        if (!emit(f, s, label, (uint64_t)data + w * height, 768, total)) return false;
    }
    for (i = 0; i < skinbytes / 2; ++i)
        if (stop(pd) || !pm_read(f, (int64_t)skins + i * 2, p, 2) || xx_data_get_u16(p, 2, 0, false) >= count) return false;
    if (skinbytes && !emit(f, s, "skin-families.bin", skins, skinbytes, total)) {
        return false;
    }
    s->size = total;
    return true;
}

void xx_valve_studio_mdl_init(xx_valve_studio_mdl *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_VALVE_STUDIO_MDL, "mdl");
    }
}
xx_valve_studio_mdl *xx_valve_studio_mdl_create(xx_io_device *d, int64_t b)
{
    xx_valve_studio_mdl *r = (xx_valve_studio_mdl *)xx_mem_alloc(sizeof(*r));
    if (r) xx_valve_studio_mdl_init(r, d, b);
    return r;
}
void xx_valve_studio_mdl_destroy(xx_valve_studio_mdl *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_valve_studio_mdl_free(xx_valve_studio_mdl *r)
{
    if (r) {
        xx_valve_studio_mdl_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_valve_studio_mdl_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_valve_studio_mdl_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
