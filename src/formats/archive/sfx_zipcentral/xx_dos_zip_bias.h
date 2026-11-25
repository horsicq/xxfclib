/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * A DOS ZIP may retain offsets from an earlier stub size. The common positive
 * offset bias lies inside the current DOS image, before all ZIP members. */
#ifndef XX_DOS_ZIP_BIAS_H
#define XX_DOS_ZIP_BIAS_H
static int sfx_dos_zip_bias_ensure(xx_sfx_zipcentral *r, xx_pd_struct *pd)
{
    Abstractformat *f = &r->format;
    uint8_t h[64], end[22], magic[4];
    int64_t size = pm_available(f), image, low, eocd, directory, bias, first, legacy;
    uint16_t count, pages, last;
    uint32_t bytes, stored, extension;
    wchar_t name[256];
    sfx_carrier_view *view = NULL;
    bool initialized = false, ok = false;
    if (size < 86 || !pm_read(f, 0, h, sizeof(h)) || h[0] != 'M' || h[1] != 'Z') return 0;
    pages = xx_data_get_u16(h + 4, 2, 0, false);
    last = xx_data_get_u16(h + 2, 2, 0, false);
    if (!pages || last > 511) return 0;
    image = ((int64_t)pages - 1) * 512 + (last ? last : 512);
    extension = xx_data_get_u32(h + 60, 4, 0, false);
    if (extension >= 64 && extension < (uint64_t)size - 4 && pm_read(f, extension, magic, 4) &&
        (!xx_rt_memcmp(magic, "PE\0\0", 4) || !xx_rt_memcmp(magic, "NE", 2) || !xx_rt_memcmp(magic, "LE", 2) || !xx_rt_memcmp(magic, "LX", 2)))
        return 0;
    eocd = size - 22;
    if (!pm_read(f, eocd, end, sizeof(end)) || xx_rt_memcmp(end, "PK\5\6", 4) || xx_data_get_u16(end + 4, 2, 0, false) || xx_data_get_u16(end + 6, 2, 0, false) ||
        xx_data_get_u16(end + 20, 2, 0, false))
        return 0;
    count = xx_data_get_u16(end + 10, 2, 0, false);
    bytes = xx_data_get_u32(end + 12, 4, 0, false);
    stored = xx_data_get_u32(end + 16, 4, 0, false);
    if (!count || count > 4096U || count != xx_data_get_u16(end + 8, 2, 0, false) || bytes > (uint64_t)eocd) return 0;
    directory = eocd - bytes;
    bias = directory - stored;
    /* Ordinary absolute and tail-relative cases keep their existing paths. */
    if (bias <= 0 || bias >= image) return 0;
    if (carrier_stop(pd) || !executable_carrier_carrier(f, &low, false, false, pd) || low != image || image >= directory) goto done;
    view = (sfx_carrier_view *)xx_mem_alloc(sizeof(*view));
    if (!view) goto done;
    sfx_carrier_view_init(view, f->device, f->base_address + bias, size - bias);
    xx_zip_init(&r->inner, &view->device, 0);
    r->inner.allow_leading_data = true;
    initialized = true;
    if (!sfx_carrier_absolute_directory(&r->inner.format, low - bias, stored, eocd - bias, count, &first, &legacy, name, pd) || first != low - bias || legacy >= 0 ||
        !xx_zip_handle_base_info(&r->inner.format, pd) || r->inner.format.format_size != size - bias || r->inner.format.number_of_archive_records != count ||
        r->inner.cd_offset != stored || r->inner.eocd_offset != eocd - bias || r->inner.directory_damaged)
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
