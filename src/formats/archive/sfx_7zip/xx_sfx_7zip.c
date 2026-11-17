/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/archive/sfx_7zip/xx_sfx_7zip.h"
#include "../../common/xx_executable_carrier.h"
#include "../../common/xx_pe_resource_locator.h"

#include "xxfclib/formats/archive/7zip/xx_7zip.h"
static bool executable_carrier_at_parse(Abstractformat *f, pm_stream *s, int64_t at, xx_pd_struct *pd)
{
    xx_7zip *r;
    bool ok;
    int64_t size;
    uint8_t h[32], kind;
    uint64_t off, n;
    if (!pm_read(f, at, h, 32) || (n = xx_data_get_u64(h + 20, 8, 0, false)) > 4194304 || !(off = xx_data_get_u64(h + 12, 8, 0, false)) ||
        !carrier_range(pm_available(f), at + 32, off) || !carrier_range(pm_available(f), at + 32 + (int64_t)off, n) || !pm_read(f, at + 32 + (int64_t)off, &kind, 1) ||
        (kind != 1 && kind != 23))
        return false;
    if (carrier_stop(pd)) {
        return false;
    }
    r = xx_7zip_create(f->device, f->base_address + at);
    if (!r) return false;
    ok = xx_format_handle_base_info(&r->format, pd);
    size = r->format.format_size;
    ok = ok && !carrier_stop(pd) && r->format.number_of_archive_records > 0 && r->format.number_of_archive_records <= 65536 &&
         carrier_range(pm_available(f), at, (uint64_t)size);
    xx_7zip_free(r);
    return ok && executable_carrier_component(f, s, at, size, "payload.7z");
}
/* The inner archive must begin at a registered PE resource-data entry, and
 * its complete physical extent must fit that resource. A signature elsewhere
 * in a section is insufficient. The existing 7z reader checks both headers. */
static bool sfx7_resource_at_parse(Abstractformat *f, pm_stream *s, int64_t at, xx_pd_struct *pd)
{
    uint8_t h[32];
    uint64_t offset, bytes, extent;
    int64_t image_end, cabinet, cabinet_end;
    if (carrier_stop(pd) || !pm_read(f, at, h, sizeof(h))) return false;
    offset = xx_data_get_u64(h + 12, 8, 0, false);
    bytes = xx_data_get_u64(h + 20, 8, 0, false);
    if (offset > UINT32_MAX - 32U || bytes > UINT32_MAX - 32U - offset) return false;
    extent = 32U + offset + bytes;
    if (!xx_sfx_pe_resource_contains(f, at, (uint32_t)extent) || carrier_stop(pd) ||
        !carrier_pe(f, &image_end, &cabinet, &cabinet_end, pd) ||
        !executable_carrier_at_parse(f, s, at, pd)) return false;
    if (s->size < image_end) s->size = image_end;
    return true;
}
#include "xx_sfx_7zip_resources.h"
#include "xx_sfx_7zip_packed_pe.h"
/* Some early stubs declare a SizeOfHeaders that is larger than the file
 * offset of their first section (4096 against a 512-byte file alignment and a
 * first section at 1024). The general PE carrier check refuses a section that
 * begins inside the declared headers, so the overlay of such a stub is never
 * scanned. Only for that one anomaly the image end is measured here with the
 * section table itself as the lower bound: every section must still lie
 * behind the table, inside the file, and overlap no other section. */
static bool sfx7_short_headers_overlay(Abstractformat *f, int64_t *overlay, xx_pd_struct *pd)
{
    uint8_t dos[64], coff[24], opt[64], row[40];
    carrier_extent spans[96];
    int64_t limit = pm_available(f), end, table_end;
    uint32_t pe, headers, first = UINT32_MAX;
    uint16_t count, optional, magic;
    unsigned i;
    if (carrier_stop(pd) || !pm_read(f, 0, dos, 64) || xx_rt_memcmp(dos, "MZ", 2)) return false;
    pe = xx_data_get_u32(dos + 60, 4, 0, false);
    if (pe < 64 || pe > 1048576 || !pm_read(f, pe, coff, 24) || xx_rt_memcmp(coff, "PE\0\0", 4)) return false;
    count = xx_data_get_u16(coff + 6, 2, 0, false);
    optional = xx_data_get_u16(coff + 20, 2, 0, false);
    if (!count || count > 96 || optional < 64 || optional > 4096 || !pm_read(f, (int64_t)pe + 24, opt, 64)) return false;
    magic = xx_data_get_u16(opt, 2, 0, false);
    headers = xx_data_get_u32(opt + 60, 4, 0, false);
    table_end = (int64_t)pe + 24 + optional + (int64_t)count * 40;
    if ((magic != 0x10b && magic != 0x20b) || table_end > limit || (int64_t)headers > limit) return false;
    end = table_end;
    for (i = 0; i < count; ++i) {
        uint32_t bytes, at;
        if (carrier_stop(pd) || !pm_read(f, table_end - (int64_t)(count - i) * 40, row, 40)) return false;
        bytes = xx_data_get_u32(row + 16, 4, 0, false);
        at = xx_data_get_u32(row + 20, 4, 0, false);
        if (bytes && ((int64_t)at < table_end || !carrier_range(limit, at, bytes))) return false;
        spans[i].lo = bytes ? (int64_t)at : limit;
        spans[i].hi = bytes ? (int64_t)at + bytes : limit;
        if (bytes && at < first) first = at;
        if (bytes && (int64_t)at + bytes > end) end = (int64_t)at + bytes;
    }
    /* The anomaly itself: otherwise the general scan has already run. */
    if (first == UINT32_MAX || first >= headers || !carrier_extents(spans, count, pd)) return false;
    *overlay = end;
    return true;
}
static bool sfx7_short_headers_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c};
    int64_t low, end, limit = pm_available(f);
    unsigned candidates = 0;
    if (!sfx7_short_headers_overlay(f, &low, pd) || low >= limit) return false;
    end = limit - low > 16777216 ? low + 16777216 : limit;
    while (low <= end - 6 && !carrier_stop(pd)) {
        int64_t at = xx_io_find_bytes_buffer_optimize_ex(f->device, f->base_address + low, end - low, sig, sizeof(sig), xx_get_file_buffer_size(), pd);
        if (at < 0 || ++candidates > 8) return false;
        at -= f->base_address;
        if (executable_carrier_at_parse(f, s, at, pd)) return true;
        if (s->count) return false;
        low = at + 1;
    }
    return false;
}
static bool sfx_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {55, 122, 188, 175, 39, 28};
    if (executable_carrier_scan(f, s, sig, sizeof(sig), 0, false, false, executable_carrier_at_parse, pd)) return true;
    if (s->count || carrier_stop(pd)) return false;
    if (sfx7_packed_overlay_parse(f, s, pd)) return true;
    if (s->count || carrier_stop(pd)) return false;
    if (sfx7_resources(f, s, pd)) return true;
    if (s->count || carrier_stop(pd)) return false;
    return sfx7_short_headers_parse(f, s, pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return sfx_carrier_parse(f, s, pd) && carrier_members(s, pd);
}

#include "xx_sfx_7zip_bundle.h"
void xx_sfx_7zip_init(xx_sfx_7zip *r, xx_io_device *d, int64_t b)
{
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_7ZIP, "exe");
    r->format.check_is_valid = xx_sfx_7zip_check_is_valid;
    r->format.handle_base_info = xx_sfx_7zip_handle_base_info;
    r->format.get_format_size = sfx7_size;
    r->format.get_number_of_archive_records = sfx7_count;
    r->format.create_archive_records_reading = sfx7_records;
    r->format.get_current_archive_record = sfx7_current;
    r->format.archive_record_move_to_next = sfx7_next;
    r->format.unpack_current_archive_record = sfx7_unpack;
    r->format.free_archive_records_reading = sfx7_free_records;
}
xx_sfx_7zip *xx_sfx_7zip_create(xx_io_device *d, int64_t b)
{
    xx_sfx_7zip *r = (xx_sfx_7zip *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_7zip_init(r, d, b);
    return r;
}
void xx_sfx_7zip_destroy(xx_sfx_7zip *r)
{
    if (!r) return;
    if (r->nested_7zip) {
        sfx7_bundle_free((sfx7_bundle *)r->nested_7zip);
        r->nested_7zip = NULL;
    }
    xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_7zip_free(xx_sfx_7zip *r)
{
    if (r) {
        xx_sfx_7zip_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_7zip_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sfx_7zip_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_sfx_7zip *outer = (xx_sfx_7zip *)f;
    pm_stream *s;
    sfx7_bundle *bundle;
    int64_t end;
    uint64_t records = 0;
    if (!outer || carrier_stop(pd)) return false;
    if (outer->nested_7zip) return true;
    s = pm_open(f, pd);
    if (!s) return false;
    if (!s->count || s->count > 128U) { pm_free_stream(s); return false; }
    bundle = (sfx7_bundle *)xx_mem_calloc(1, sizeof(*bundle));
    if (!bundle) { pm_free_stream(s); return false; }
    for (size_t i = 0; i < s->count; ++i) {
        xx_7zip *nested = s->items[i].offset >= f->base_address ? xx_7zip_create(f->device, s->items[i].offset) : NULL;
        if (!nested || carrier_stop(pd) || !xx_7zip_handle_base_info(&nested->format, pd) ||
            nested->format.format_size != s->items[i].size || nested->format.number_of_archive_records > 65536U - records) {
            if (nested) xx_7zip_free(nested);
            sfx7_bundle_free(bundle); pm_free_stream(s); return false;
        }
        bundle->readers[bundle->count++] = nested;
        records += nested->format.number_of_archive_records;
    }
    f->format_size = s->size;
    f->number_of_archive_records = records;
    end = f->base_address + s->size;
    f->overlay_size = xx_io_size(f->device) - end;
    f->overlay_offset = f->overlay_size > 0 ? end : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    outer->nested_7zip = bundle;
    pm_free_stream(s);
    return true;
}
