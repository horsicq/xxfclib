/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/archive/sfx_arj/xx_sfx_arj.h"
#include "../../common/xx_sfx_carrier.h"
#include "../../common/xx_pe_resource_archives.h"

#include "xxfclib/formats/archive/arj/xx_arj.h"
static Abstractformat *nested_open(xx_io_device *d, int64_t at)
{
    xx_arj *r = xx_arj_create(d, at);
    return r ? &r->format : NULL;
}
static void nested_close(Abstractformat *f)
{
    xx_arj_free((xx_arj *)f);
}
static bool arj_resource_candidate(Abstractformat *f, pm_stream *s, int64_t at, uint32_t bytes, xx_pd_struct *pd)
{
    if (carrier_stop(pd) || bytes < 40 || bytes > 256U * 1024U * 1024U) return false;
    xx_io_device *view = xx_io_sub_open_ro(f->device, f->base_address + at, bytes);
    if (!view) return false;
    xx_arj *r = xx_arj_create(view, 0);
    int64_t size = 0;
    bool ok = r && xx_arj_handle_base_info(&r->format, pd) && r->format.number_of_archive_records > 0 && r->format.number_of_archive_records <= 65536;
    if (ok) {
        size = r->format.format_size;
        ok = size > 0 && size <= bytes && !carrier_stop(pd);
    }
    if (r) xx_arj_free(r);
    xx_io_close(view);
    return ok && pm_add(f, s, "payload.arj", at, size);
}
static bool sfx_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {0x60, 0xea};
    int64_t low;
    if (!sfx_carrier_carrier(f, false, &low, pd)) return false;
    if (sfx_carrier_embedded(f, s, low, sig, sizeof(sig), 0, nested_open, nested_close, "payload.arj", pd)) return true;
    if (s->count || carrier_stop(pd)) return false;
    return xx_sfx_pe_resource_archives(f, s, sig, sizeof(sig), arj_resource_candidate, pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return sfx_carrier_parse(f, s, pd) && carrier_members(s, pd);
}
#include "xx_sfx_arj_bundle.h"
void xx_sfx_arj_init(xx_sfx_arj *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_ARJ, "exe");
        r->format.get_format_size = arjb_size;
        r->format.get_number_of_archive_records = arjb_count;
        r->format.create_archive_records_reading = arjb_records;
        r->format.get_current_archive_record = arjb_current;
        r->format.archive_record_move_to_next = arjb_next;
        r->format.unpack_current_archive_record = arjb_unpack;
        r->format.free_archive_records_reading = arjb_free_records;
    }
}
xx_sfx_arj *xx_sfx_arj_create(xx_io_device *d, int64_t b)
{
    xx_sfx_arj *r = (xx_sfx_arj *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_arj_init(r, d, b);
    return r;
}
void xx_sfx_arj_destroy(xx_sfx_arj *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_arj_free(xx_sfx_arj *r)
{
    if (r) {
        xx_sfx_arj_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_arj_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sfx_arj_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    arjb_bundle *b = arjb_open(f, pd);
    if (!b) return false;
    f->format_size = b->extent;
    f->number_of_archive_records = b->total;
    int64_t end = f->base_address + b->extent;
    f->overlay_size = xx_io_size(f->device) - end;
    f->overlay_offset = f->overlay_size > 0 ? end : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    arjb_bundle_free(b);
    return true;
}
