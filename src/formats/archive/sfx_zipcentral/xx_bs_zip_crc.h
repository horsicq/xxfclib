/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * BSsfx stores placeholder central CRCs; authentic local-header CRCs remain
 * mandatory for ordinary ZIP decoding. Source bytes are never modified. */
#ifndef XX_BS_ZIP_CRC_H
#define XX_BS_ZIP_CRC_H
typedef struct sfx_bs_crc_patch {
    int64_t offset;
    uint32_t value;
} sfx_bs_crc_patch;
typedef struct sfx_bs_crc_view {
    sfx_carrier_view view;
    unsigned count;
    sfx_bs_crc_patch patches[];
} sfx_bs_crc_view;
static ssize_t sfx_bs_crc_read(xx_io_device *device, void *buffer, size_t size)
{
    sfx_bs_crc_view *v = (sfx_bs_crc_view *)device->priv;
    int64_t start = v->view.position;
    ssize_t got = sfx_carrier_view_read(device, buffer, size);
    unsigned i, j;
    if (got <= 0) return got;
    for (i = 0; i < v->count; ++i)
        for (j = 0; j < 4; ++j) {
            int64_t at = v->patches[i].offset + j;
            if (at >= start && at - start < got) ((uint8_t *)buffer)[at - start] = (uint8_t)(v->patches[i].value >> (j * 8U));
        }
    return got;
}
static bool sfx_bs_marker(Abstractformat *f, int64_t end, xx_pd_struct *pd)
{
    uint8_t data[4096];
    int64_t at;
    if (end > 1024 * 1024) return false;
    for (at = 0; at <= end - 5; at += 4092) {
        size_t n = end - at > 4096 ? 4096U : (size_t)(end - at), i;
        if (carrier_stop(pd) || !pm_read(f, at, data, n)) return false;
        for (i = 0; i + 5 <= n; ++i)
            if (!xx_rt_memcmp(data + i, "BSsfx", 5)) return true;
    }
    return false;
}
static int sfx_bs_zip_crc_ensure(xx_sfx_zipcentral *r, xx_pd_struct *pd)
{
    Abstractformat *f = &r->format;
    uint8_t h[64], end[22], cd[46], local[30], magic[4];
    int64_t size = pm_available(f), eocd, dir, low, cp, first, legacy;
    uint32_t pe_at, bytes;
    uint16_t count;
    wchar_t legacy_name[256];
    sfx_bs_crc_view *view = NULL;
    bool initialized = false, ok = false;
    unsigned i;
    if (size < 86 || !pm_read(f, 0, h, sizeof(h)) || h[0] != 'M' || h[1] != 'Z') return 0;
    pe_at = xx_data_get_u32(h + 60, 4, 0, false);
    if (pe_at < 64 || pe_at > (uint64_t)size - 4 || !pm_read(f, pe_at, magic, 4) || xx_rt_memcmp(magic, "PE\0\0", 4)) return 0;
    eocd = size - 22;
    if (!pm_read(f, eocd, end, sizeof(end)) || xx_rt_memcmp(end, "PK\5\6", 4) || xx_data_get_u16(end + 4, 2, 0, false) || xx_data_get_u16(end + 6, 2, 0, false) ||
        xx_data_get_u16(end + 20, 2, 0, false))
        return 0;
    count = xx_data_get_u16(end + 10, 2, 0, false);
    bytes = xx_data_get_u32(end + 12, 4, 0, false);
    dir = xx_data_get_u32(end + 16, 4, 0, false);
    if (!count || count > 4096U || count != xx_data_get_u16(end + 8, 2, 0, false) || dir > eocd || bytes != (uint64_t)(eocd - dir) || bytes < 46 ||
        !pm_read(f, dir, cd, sizeof(cd)) || xx_rt_memcmp(cd, "PK\1\2", 4) || xx_data_get_u32(cd + 16, 4, 0, false) != 40U ||
        !executable_carrier_carrier(f, &low, false, false, pd) || !sfx_bs_marker(f, low, pd))
        return 0;
    view = (sfx_bs_crc_view *)xx_mem_alloc(sizeof(*view) + (size_t)count * sizeof(view->patches[0]));
    if (!view) goto done;
    sfx_carrier_view_init(&view->view, f->device, f->base_address, size);
    view->count = 0;
    view->view.device.read = sfx_bs_crc_read;
    cp = dir;
    for (i = 0; i < count; ++i) {
        uint16_t fn;
        uint32_t crc, packed, raw;
        int64_t at;
        if (carrier_stop(pd) || cp > eocd - 46 || !pm_read(f, cp, cd, sizeof(cd)) || xx_rt_memcmp(cd, "PK\1\2", 4)) goto done;
        fn = xx_data_get_u16(cd + 28, 2, 0, false);
        at = xx_data_get_u32(cd + 42, 4, 0, false);
        packed = xx_data_get_u32(cd + 20, 4, 0, false);
        raw = xx_data_get_u32(cd + 24, 4, 0, false);
        if (xx_data_get_u16(cd + 4, 2, 0, false) != 20 || xx_data_get_u16(cd + 6, 2, 0, false) != 20 || xx_data_get_u16(cd + 8, 2, 0, false) ||
            xx_data_get_u16(cd + 10, 2, 0, false) != 8 || xx_data_get_u32(cd + 16, 4, 0, false) != (i ? 0U : 40U) || xx_data_get_u16(cd + 30, 2, 0, false) ||
            xx_data_get_u16(cd + 32, 2, 0, false) || xx_data_get_u16(cd + 34, 2, 0, false) || xx_data_get_u16(cd + 36, 2, 0, false) ||
            xx_data_get_u32(cd + 38, 4, 0, false) != 32U || !fn || fn > 4096U || (int64_t)46 + fn > eocd - cp || at < low || at > dir - 30 ||
            !pm_read(f, at, local, sizeof(local)) || xx_rt_memcmp(local, "PK\3\4", 4) || xx_data_get_u16(local + 4, 2, 0, false) != 20 ||
            xx_data_get_u16(local + 6, 2, 0, false) || xx_data_get_u16(local + 8, 2, 0, false) != 8 || xx_data_get_u16(local + 26, 2, 0, false) != fn ||
            xx_data_get_u16(local + 28, 2, 0, false) || xx_data_get_u32(local + 18, 4, 0, false) != packed || xx_data_get_u32(local + 22, 4, 0, false) != raw ||
            xx_rt_memcmp(local + 10, cd + 12, 4) || (uint64_t)30 + fn + packed > (uint64_t)(dir - at) || !carrier_equal(f, at + 30, cp + 46, fn, pd))
            goto done;
        crc = xx_data_get_u32(local + 14, 4, 0, false);
        if (!crc) goto done;
        view->patches[view->count].offset = cp + 16;
        view->patches[view->count++].value = crc;
        cp += 46 + fn;
    }
    if (cp != eocd) goto done;
    xx_zip_init(&r->inner, &view->view.device, 0);
    r->inner.allow_leading_data = true;
    initialized = true;
    if (!sfx_carrier_absolute_directory(&r->inner.format, low, dir, eocd, count, &first, &legacy, legacy_name, pd) || first != low || legacy >= 0 ||
        !xx_zip_handle_base_info(&r->inner.format, pd) || r->inner.cd_offset != dir || r->inner.eocd_offset != eocd || r->inner.format.format_size != size ||
        r->inner.format.number_of_archive_records != count || r->inner.directory_damaged)
        goto done;
    r->relative_view = view;
    view = NULL;
    r->inner_ready = true;
    ok = true;
done:
    if (!ok && initialized) xx_zip_destroy(&r->inner);
    xx_mem_free(view);
    if (!ok) f->priv = (void *)&sfx_zip_resource_invalid;
    return ok ? 1 : -1;
}
#endif
