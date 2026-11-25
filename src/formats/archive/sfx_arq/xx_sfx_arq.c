/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/archive/sfx_arq/xx_sfx_arq.h"
#include "../../common/xx_sfx_carrier.h"

#include "xxfclib/formats/archive/arq/xx_arq.h"
static Abstractformat *nested_open(xx_io_device *d, int64_t at)
{
    xx_arq *r = xx_arq_create(d, at);
    return r ? &r->format : NULL;
}
static void nested_close(Abstractformat *f)
{
    xx_arq_free((xx_arq *)f);
}
static bool sfx_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {0x67, 0x57, 0x04, 0x02};
    int64_t low;
    if (!sfx_carrier_carrier(f, false, &low, pd)) return false;
    return sfx_carrier_embedded(f, s, low, sig, sizeof(sig), 0, nested_open, nested_close, "payload.arq", pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return sfx_carrier_parse(f, s, pd) && carrier_members(s, pd);
}
#include "xx_sfx_arq_members.h"
void xx_sfx_arq_init(xx_sfx_arq *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SFX_ARQ, "exe");
        r->format.handle_base_info = xx_sfx_arq_handle_base_info;
        r->format.get_format_size = arqm_size;
        r->format.get_number_of_archive_records = arqm_count;
        r->format.create_archive_records_reading = arqm_records;
        r->format.get_current_archive_record = arqm_current;
        r->format.archive_record_move_to_next = arqm_next;
        r->format.unpack_current_archive_record = arqm_unpack;
        r->format.free_archive_records_reading = arqm_free_records;
    }
}
xx_sfx_arq *xx_sfx_arq_create(xx_io_device *d, int64_t b)
{
    xx_sfx_arq *r = (xx_sfx_arq *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_arq_init(r, d, b);
    return r;
}
void xx_sfx_arq_destroy(xx_sfx_arq *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_arq_free(xx_sfx_arq *r)
{
    if (r) {
        xx_sfx_arq_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_arq_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sfx_arq_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    int64_t extent = 0;
    xx_arq *r = arqm_open(f, &extent, pd);
    if (!r) return false;
    f->format_size = extent;
    f->number_of_archive_records = r->format.number_of_archive_records;
    int64_t end = f->base_address + extent;
    f->overlay_size = xx_io_size(f->device) - end;
    f->overlay_offset = f->overlay_size > 0 ? end : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    xx_arq_free(r);
    return true;
}
