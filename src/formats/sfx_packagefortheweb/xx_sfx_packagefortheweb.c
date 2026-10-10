/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/installers/xpftw.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#include "xxfclib/formats/sfx_packagefortheweb/xx_sfx_packagefortheweb.h"
#include "../common/xx_carrier_helpers.h"

static bool pw_cab(Abstractformat *f, int64_t at, int64_t end, int64_t *stop, xx_pd_struct *pd)
{
    uint8_t h[36];
    uint32_t bytes, filesat;
    uint16_t folders, files, flags, i;
    int64_t table, first_data;
    uint8_t reserve = 0, data_reserve = 0;
    uint16_t header_reserve = 0;
    if (end - at < 36 || !pm_read(f, at, h, 36) || xx_rt_memcmp(h, "MSCF", 4) || xx_data_get_u32(h + 4, 4, 0, false) || xx_data_get_u32(h + 12, 4, 0, false) ||
        xx_data_get_u32(h + 20, 4, 0, false) || h[24] != 3 || h[25] != 1)
        return false;
    bytes = xx_data_get_u32(h + 8, 4, 0, false);
    filesat = xx_data_get_u32(h + 16, 4, 0, false);
    folders = xx_data_get_u16(h + 26, 2, 0, false);
    files = xx_data_get_u16(h + 28, 2, 0, false);
    flags = xx_data_get_u16(h + 30, 2, 0, false);
    if (bytes < 36 || !carrier_range(end, at, bytes) || !folders || folders > 4096 || !files || (flags & ~4U)) {
        return false;
    }
    *stop = at + bytes;
    table = at + 36;
    first_data = *stop;
    if (flags & 4) {
        if (!carrier_range(*stop, table, 4) || !pm_read(f, table, h, 4)) return false;
        header_reserve = xx_data_get_u16(h, 2, 0, false);
        reserve = h[2];
        data_reserve = h[3];
        table += 4 + header_reserve;
    }
    if (table > *stop || (uint64_t)folders * (8U + reserve) > (uint64_t)(*stop - table) || filesat < (uint64_t)(table - at) + (uint64_t)folders * (8U + reserve) ||
        filesat >= bytes)
        return false;
    for (i = 0; i < folders; ++i) {
        uint32_t off;
        uint16_t chunks, j;
        int64_t p;
        if (carrier_stop(pd) || !pm_read(f, table + (int64_t)i * (8 + reserve), h, 8)) {
            return false;
        }
        off = xx_data_get_u32(h, 4, 0, false);
        chunks = xx_data_get_u16(h + 4, 2, 0, false);
        if (off >= bytes || !chunks || (xx_data_get_u16(h + 6, 2, 0, false) & 15) > 3) return false;
        p = at + off;
        if (p < first_data) first_data = p;
        for (j = 0; j < chunks; ++j) {
            uint16_t packed;
            if (*stop - p < 8 + data_reserve || !pm_read(f, p, h, 8) || !(packed = xx_data_get_u16(h + 4, 2, 0, false)) ||
                packed > (uint64_t)(*stop - p - 8 - data_reserve))
                return false;
            p += 8 + data_reserve + packed;
        }
    }
    table = at + filesat;
    for (i = 0; i < files; ++i) {
        char name[4097];
        if (carrier_stop(pd) || first_data - table < 16 || !pm_read(f, table, h, 16) || xx_data_get_u16(h + 8, 2, 0, false) >= folders) return false;
        table += 16;
        if (!carrier_string(f, &table, first_data, name, sizeof(name)) || !name[0]) return false;
    }
    return table <= first_data;
}
static bool carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t overlay, cab, section_end, end, limit = pm_available(f);
    uint8_t h[16];
    unsigned attempt;
    uint32_t size;
    uint8_t *settings = NULL;
    bool ok = false;
    if (!carrier_pe(f, &overlay, &cab, &section_end, pd)) return false;
    for (attempt = 0; attempt < (cab >= 0 ? 2U : 1U); ++attempt) {
        int64_t start = cab >= 0 ? cab + (attempt ? 12 : 0) : overlay, region_end = cab >= 0 ? section_end : limit;
        unsigned i;
        if (!pm_read(f, start, h, 4) || (size = xx_data_get_u32(h, 4, 0, false)) < 16 || size > 1048576 || !carrier_range(region_end, start + 4, (uint64_t)size + 36))
            continue;
        settings = (uint8_t *)xx_mem_alloc(size);
        if (!settings || !pm_read(f, start + 4, settings, size)) goto done;
        for (i = 0; i < size; ++i) settings[i] ^= (uint8_t)(0x61 + i % 26);
        if (xx_rt_memcmp(settings, "SCG", 3) || !pw_cab(f, start + 4 + size, region_end, &end, pd) ||
            xx_data_get_u32(settings + 4, 4, 0, false) != (uint64_t)(end - start - 4 - size)) {
            xx_mem_free(settings);
            settings = NULL;
            continue;
        }
        if (!pm_add(f, s, "settings.scg", start + 4, size)) {
            goto done;
        }
        s->items[s->count - 1].memory = settings;
        settings = NULL;
        if (!pm_add(f, s, "payload.cab", start + 4 + size, end - start - 4 - size)) {
            goto done;
        }
        s->size = cab >= 0 && overlay > end ? overlay : end;
        ok = true;
        break;
    }
done:
    if (settings) xx_mem_free(settings);
    return ok;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return carrier_parse(f, s, pd) && carrier_members(s, pd);
}
void xx_sfx_packagefortheweb_init(xx_sfx_packagefortheweb *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_PACKAGEFORTHEWEB, "exe");
    }
}
xx_sfx_packagefortheweb *xx_sfx_packagefortheweb_create(xx_io_device *d, int64_t b)
{
    xx_sfx_packagefortheweb *r = (xx_sfx_packagefortheweb *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_packagefortheweb_init(r, d, b);
    return r;
}
void xx_sfx_packagefortheweb_destroy(xx_sfx_packagefortheweb *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_packagefortheweb_free(xx_sfx_packagefortheweb *r)
{
    if (r) {
        xx_sfx_packagefortheweb_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_packagefortheweb_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sfx_packagefortheweb_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
