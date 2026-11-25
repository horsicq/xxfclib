/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT
 * PKLITE ZIP SFX with an unchanged directory repeated in appended data.
 * Both indexes must be identical and refer to the same complete original ZIP. */
#ifndef XX_DOS_ZIP_DUPLICATE_H
#define XX_DOS_ZIP_DUPLICATE_H
static int sfx_dos_zip_duplicate_ensure(xx_sfx_zipcentral *r, xx_pd_struct *pd)
{
    Abstractformat *f = &r->format;
    uint8_t h[64], *tail;
    int64_t size = pm_available(f), low, begin;
    size_t n, i;
    uint64_t work = UINT64_C(67108864);
    bool recognized = false;
    int result = 0;
    if (size < 108 || !pm_read(f, 0, h, sizeof(h)) || h[0] != 'M' || h[1] != 'Z' || xx_rt_memcmp(h + 30, "PKLITE", 6) ||
        !executable_carrier_carrier(f, &low, false, false, pd))
        return 0;
    {
        uint16_t pages = xx_data_get_u16(h + 4, 2, 0, false), last = xx_data_get_u16(h + 2, 2, 0, false);
        if (!pages || last > 511 || low != ((int64_t)pages - 1) * 512 + (last ? last : 512)) return 0;
    }
    begin = size > 65557 ? size - 65557 : 0;
    n = (size_t)(size - begin);
    tail = (uint8_t *)xx_mem_alloc(n);
    if (!tail || !pm_read(f, begin, tail, n)) {
        xx_mem_free(tail);
        return 0;
    }
    for (i = n - 21; i > 0; --i) {
        size_t p = i - 1;
        uint8_t original[22];
        uint16_t count;
        uint32_t bytes, stored;
        int64_t candidate, copied, end, first, legacy;
        wchar_t name[256];
        sfx_carrier_view *view;
        if (carrier_stop(pd)) break;
        if (xx_rt_memcmp(tail + p, "PK\5\6", 4) || xx_data_get_u16(tail + p + 4, 2, 0, false) || xx_data_get_u16(tail + p + 6, 2, 0, false) ||
            xx_data_get_u16(tail + p + 20, 2, 0, false))
            continue;
        count = xx_data_get_u16(tail + p + 10, 2, 0, false);
        bytes = xx_data_get_u32(tail + p + 12, 4, 0, false);
        stored = xx_data_get_u32(tail + p + 16, 4, 0, false);
        candidate = begin + (int64_t)p;
        if (!count || count > 4096U || count != xx_data_get_u16(tail + p + 8, 2, 0, false) || !bytes || bytes > 16U * 1024U * 1024U || bytes > (uint64_t)candidate ||
            stored < (uint64_t)low)
            continue;
        copied = candidate - bytes;
        end = (int64_t)stored + bytes;
        if (end + 22 > copied || end > size - 22 || size - end - 22 > 16 * 1024 * 1024 || !pm_read(f, end, original, sizeof(original)) ||
            xx_rt_memcmp(original, tail + p, 22))
            continue;
        if (bytes > work) break;
        work -= bytes;
        if (!carrier_equal(f, stored, copied, bytes, pd)) continue;
        recognized = true;
        if (!sfx_carrier_absolute_directory(f, low, stored, end, count, &first, &legacy, name, pd) || first != low || legacy >= 0) continue;
        view = (sfx_carrier_view *)xx_mem_alloc(sizeof(*view));
        if (!view) break;
        sfx_carrier_view_init(view, f->device, f->base_address, end + 22);
        xx_zip_init(&r->inner, &view->device, 0);
        r->inner.allow_leading_data = true;
        if (xx_zip_handle_base_info(&r->inner.format, pd) && r->inner.format.format_size == end + 22 && r->inner.format.number_of_archive_records == count &&
            r->inner.cd_offset == stored && r->inner.eocd_offset == end && !r->inner.directory_damaged) {
            r->relative_view = view;
            r->inner_ready = true;
            result = 1;
            break;
        }
        xx_zip_destroy(&r->inner);
        xx_mem_free(view);
    }
    xx_mem_free(tail);
    if (!result && recognized) f->priv = (void *)&sfx_zip_resource_invalid;
    return result ? result : (recognized || carrier_stop(pd) ? -1 : 0);
}
#endif
