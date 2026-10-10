/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xtgcfarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_tgcf/xx_sfx_tgcf.h"
#include "../common/xx_archive_carrier_readers.h"

static bool archive_carrier_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    static const uint8_t sig[] = {0x54, 0x47, 0x43, 0x46};
    return archive_carrier_carried(f, s, sig, sizeof(sig), 0, 9, archive_carrier_tgcf, "payload.tgcf", pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    return archive_carrier_parse(f, s, pd) && carrier_members(s, pd);
}

/* The carrier validates a bounded TGCF tail. Reuse its native reader so an
 * SFX listing and extraction expose the contained files in one pass. */
static bool tgcf_ensure_inner(xx_sfx_tgcf *reader, xx_pd_struct *pd)
{
    Abstractformat *outer = &reader->format;
    pm_stream *carrier;
    pm_member *payload;
    xx_archive_record_state *state = NULL;
    const xx_archive_record *record;
    int64_t end;
    uint64_t count = 0;
    bool valid = false;

    if (reader->checked_inner) return reader->inner_ready;
    reader->checked_inner = true;
    carrier = pm_open(outer, pd);
    if (!carrier) return false;
    if (carrier->count != 1 || carrier->size <= 0 || carrier->size > pm_available(outer)) goto done;
    payload = &carrier->items[0];
    if (payload->memory || payload->size <= 0 || payload->offset < outer->base_address || payload->offset > INT64_MAX - payload->size ||
        payload->offset + payload->size != outer->base_address + carrier->size)
        goto done;
    end = payload->offset + payload->size;
    xx_tgcf_init(&reader->inner, outer->device, payload->offset);
    if (!xx_tgcf_handle_base_info(&reader->inner.format, pd) || reader->inner.format.format_size <= 0 || reader->inner.format.format_size > payload->size ||
        !reader->inner.format.number_of_archive_records || reader->inner.format.number_of_archive_records > ARCHIVE_CARRIER_COUNT)
        goto discard;
    state = xx_tgcf_create_archive_records_reading(&reader->inner.format, NULL, pd);
    if (!state) goto discard;
    valid = true;
    while ((record = xx_tgcf_get_current_archive_record(&reader->inner.format, state)) != NULL) {
        if (carrier_stop(pd) || ++count > ARCHIVE_CARRIER_COUNT || record->compressed_size < 0 || record->data_offset < payload->offset || record->data_offset > end ||
            record->compressed_size > end - record->data_offset) {
            valid = false;
            break;
        }
        if (!xx_tgcf_archive_record_move_to_next(&reader->inner.format, state, pd)) break;
    }
    if (count != reader->inner.format.number_of_archive_records) valid = false;
    xx_tgcf_free_archive_records_reading(&reader->inner.format, state);
    if (valid) {
        reader->inner_ready = true;
        goto done;
    }
discard:
    xx_tgcf_destroy(&reader->inner);
done:
    pm_free_stream(carrier);
    return reader->inner_ready;
}

static int64_t tgcf_size(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_sfx_tgcf_handle_base_info(f, pd) ? f->format_size : -1;
}
static uint64_t tgcf_count(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_sfx_tgcf_handle_base_info(f, pd) ? f->number_of_archive_records : 0;
}

static xx_archive_record_state *tgcf_records(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_sfx_tgcf *reader = (xx_sfx_tgcf *)f;
    return tgcf_ensure_inner(reader, pd) ? xx_tgcf_create_archive_records_reading(&reader->inner.format, options, pd) : pm_create_records(f, options, pd);
}
static const xx_archive_record *tgcf_current(Abstractformat *f, xx_archive_record_state *state)
{
    xx_sfx_tgcf *reader = (xx_sfx_tgcf *)f;
    return reader->inner_ready ? xx_tgcf_get_current_archive_record(&reader->inner.format, state) : pm_current(f, state);
}
static bool tgcf_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_sfx_tgcf *reader = (xx_sfx_tgcf *)f;
    return reader->inner_ready ? xx_tgcf_archive_record_move_to_next(&reader->inner.format, state, pd) : pm_next(f, state, pd);
}
static bool tgcf_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_sfx_tgcf *reader = (xx_sfx_tgcf *)f;
    return reader->inner_ready ? xx_tgcf_unpack_current_archive_record(&reader->inner.format, state, pd) : pm_unpack(f, state, pd);
}
static void tgcf_free_records(Abstractformat *f, xx_archive_record_state *state)
{
    xx_sfx_tgcf *reader = (xx_sfx_tgcf *)f;
    if (reader->inner_ready) xx_tgcf_free_archive_records_reading(&reader->inner.format, state);
    else pm_free_records(f, state);
}

void xx_sfx_tgcf_init(xx_sfx_tgcf *r, xx_io_device *d, int64_t b)
{
    Abstractformat *f;
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    f = &r->format;
    pm_init(f, d, b, XX_FILE_TYPE_SFX_TGCF, "exe");
    f->check_is_valid = xx_sfx_tgcf_check_is_valid;
    f->handle_base_info = xx_sfx_tgcf_handle_base_info;
    f->get_format_size = tgcf_size;
    f->get_number_of_archive_records = tgcf_count;
    f->create_archive_records_reading = tgcf_records;
    f->get_current_archive_record = tgcf_current;
    f->archive_record_move_to_next = tgcf_next;
    f->unpack_current_archive_record = tgcf_unpack;
    f->free_archive_records_reading = tgcf_free_records;
}
xx_sfx_tgcf *xx_sfx_tgcf_create(xx_io_device *d, int64_t b)
{
    xx_sfx_tgcf *r = (xx_sfx_tgcf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_tgcf_init(r, d, b);
    return r;
}
void xx_sfx_tgcf_destroy(xx_sfx_tgcf *r)
{
    if (r) {
        if (r->inner_ready) xx_tgcf_destroy(&r->inner);
        xx_format_cleanup_extra_parameters(&r->format);
    }
}
void xx_sfx_tgcf_free(xx_sfx_tgcf *r)
{
    if (r) {
        xx_sfx_tgcf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_tgcf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return f && (tgcf_ensure_inner((xx_sfx_tgcf *)f, pd) || pm_valid(f, pd));
}
bool xx_sfx_tgcf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_sfx_tgcf *reader = (xx_sfx_tgcf *)f;
    if (!f) return false;
    if (!tgcf_ensure_inner(reader, pd)) return pm_handle(f, pd);
    f->format_size = pm_available(f);
    f->number_of_archive_records = reader->inner.format.number_of_archive_records;
    f->overlay_size = 0;
    f->overlay_offset = -1;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
