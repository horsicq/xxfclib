/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * A DOS/COM or Atari ST self-extractor carrying an LHA member chain.
 * The LHA reader owns parsing, CRC validation and member extraction; the
 * outer reader only locates the stream and keeps the source device borrowed.
 */
#include "xxfclib/formats/sfx_lha/xx_sfx_lha.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

static bool sfx_lha_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}

static int64_t sfx_lha_available(Abstractformat *f)
{
    int64_t size;
    if (!f || !f->device || f->base_address < 0 || (size = xx_io_size(f->device)) < f->base_address) return -1;
    return size - f->base_address;
}

static bool sfx_lha_read(Abstractformat *f, int64_t at, void *out, size_t size)
{
    int64_t available = sfx_lha_available(f);
    size_t capacity = xx_get_file_buffer_size(), done = 0;
    if (at < 0 || available < at || size > (uint64_t)(available - at) || (!out && size) || !capacity || xx_io_seek64(f->device, f->base_address + at, SEEK_SET) != 0)
        return false;
    while (done < size) {
        size_t request = size - done < capacity ? size - done : capacity;
        ssize_t received = xx_io_read(f->device, (uint8_t *)out + done, request);
        if (received <= 0 || (size_t)received > request) return false;
        done += (size_t)received;
    }
    return true;
}

static bool sfx_lha_try_at(xx_sfx_lha *r, int64_t at, int64_t limit, xx_pd_struct *pd)
{
    if (at < 0 || at > limit - 24 || sfx_lha_stop(pd)) return false;
    xx_lha_init(&r->inner, r->format.device, r->format.base_address + at);
    r->inner.sanitize_sfx_drive = true;
    if (!xx_lha_handle_base_info(&r->inner.format, pd) || !r->inner.format.number_of_archive_records || r->inner.format.format_size <= 0 ||
        r->inner.format.format_size > limit - at) {
        xx_lha_destroy(&r->inner);
        return false;
    }
    r->payload_offset = at;
    r->inner_ready = true;
    return true;
}

/* Atari LArc self-extractors append a GEMDOS trailer after their sole -lz5-
 * member: either four zero bytes or one relocation byte followed by four
 * zeros, sometimes padded with DOS EOF (0x1a) bytes. The ordinary LHA reader
 * rejects a nonzero next header byte.
 * Restrict this exception to that exact GEMDOS layout and expose only the
 * declared member extent through a borrowed-memory device. The inner LHA
 * parser still validates the complete header and member chain. */
static bool sfx_lha_try_atari_tail(xx_sfx_lha *r, int64_t at, int64_t limit, xx_pd_struct *pd)
{
    uint8_t prefix[24], tail[128];
    int64_t archive_span;
    int64_t tail_size;
    uint64_t packed;
    size_t header_size, i, zero_offset;
    uint8_t *bytes;
    xx_io_device *device;
    if (at < 0 || at > limit - (int64_t)sizeof(prefix) || !sfx_lha_read(&r->format, at, prefix, sizeof(prefix)) || prefix[20] != 0U ||
        xx_rt_memcmp(prefix + 2, "-lz5-", 5U) != 0)
        return false;
    header_size = (size_t)prefix[0] + 2U;
    packed = xx_data_get_u32(prefix + 7, 4, 0, false);
    if (header_size < 24U || header_size > (size_t)(limit - at) || packed > (uint64_t)(limit - at - (int64_t)header_size)) return false;
    archive_span = (int64_t)header_size + (int64_t)packed;
    tail_size = limit - at - archive_span;
    if (archive_span > 64 * 1024 * 1024 || tail_size < 4 || tail_size > (int64_t)sizeof(tail) || !sfx_lha_read(&r->format, at + archive_span, tail, (size_t)tail_size) ||
        sfx_lha_stop(pd))
        return false;
    zero_offset = tail_size == 4 ? 0U : 1U;
    for (i = zero_offset; i < zero_offset + 4U; ++i)
        if (tail[i] != 0U) return false;
    for (i = 5U; i < (size_t)tail_size; ++i)
        if (tail[i] != 0x1aU) return false;
    bytes = (uint8_t *)xx_mem_alloc((size_t)archive_span);
    if (!bytes) return false;
    if (!sfx_lha_read(&r->format, at, bytes, (size_t)archive_span) || !(device = xx_io_mem_open_ro(bytes, (size_t)archive_span))) {
        xx_mem_free(bytes);
        return false;
    }
    xx_lha_init(&r->inner, device, 0);
    r->inner.sanitize_sfx_drive = true;
    if (!xx_lha_handle_base_info(&r->inner.format, pd) || r->inner.format.number_of_archive_records != 1U || r->inner.format.format_size != archive_span) {
        xx_lha_destroy(&r->inner);
        xx_io_close(device);
        xx_mem_free(bytes);
        return false;
    }
    r->bounded_device = device;
    r->bounded_bytes = bytes;
    r->payload_offset = at;
    r->inner_ready = true;
    return true;
}

static bool sfx_lha_scan_range(xx_sfx_lha *r, int64_t start, int64_t limit, xx_pd_struct *pd)
{
    uint8_t *scan;
    size_t bytes, i;
    unsigned candidates = 0U;
    if (start < 0 || start >= limit) return false;
    bytes = (uint64_t)(limit - start) > 65536U ? 65536U : (size_t)(limit - start);
    if (bytes < 24U || !(scan = (uint8_t *)xx_mem_alloc(bytes))) return false;
    if (!sfx_lha_read(&r->format, start, scan, bytes)) {
        xx_mem_free(scan);
        return false;
    }
    for (i = 0U; i + 24U <= bytes && candidates < 16U; ++i) {
        if ((i & 4095U) == 0U && sfx_lha_stop(pd)) break;
        if (scan[i + 2U] != '-' || scan[i + 3U] != 'l' || scan[i + 6U] != '-') continue;
        ++candidates;
        if (sfx_lha_try_at(r, start + (int64_t)i, limit, pd)) break;
    }
    xx_mem_free(scan);
    return r->inner_ready;
}

static bool sfx_lha_ensure(xx_sfx_lha *r, xx_pd_struct *pd)
{
    Abstractformat *f = &r->format;
    uint8_t h[64];
    int64_t limit, start;
    bool mz_carrier = false;
    if (r->checked) return r->inner_ready;
    r->checked = true;
    limit = sfx_lha_available(f);
    if (limit < 64 || !sfx_lha_read(f, 0, h, sizeof(h))) return false;
    if (!xx_rt_memcmp(h, "MZ", 2)) {
        uint64_t paragraphs = (uint64_t)xx_data_get_u16(h + 8, 2, 0, false) * 16U;
        uint64_t pages = xx_data_get_u16(h + 4, 2, 0, false);
        uint64_t last = xx_data_get_u16(h + 2, 2, 0, false);
        uint64_t image;
        if (paragraphs < 28 || paragraphs > 65536 || pages == 0 || last > 511) return false;
        image = (pages - 1U) * 512U + (last ? last : 512U);
        if (image < paragraphs || image > (uint64_t)limit) return false;
        mz_carrier = true;
        start = image == (uint64_t)limit ? 0 : (int64_t)image;
        /* Most DOS LHA stubs put the first header exactly after their image. */
        if (start && sfx_lha_try_at(r, start, limit, pd)) return true;
    } else if (h[0] == 0xeb) {
        /* A COM self-extractor has no MZ size word; search its short stub. */
        start = 0;
    } else if (h[0] == 0x60 && h[1] == 0x1a) {
        /* Atari ST GEMDOS executables put the archive in the data section.
         * The fixed 28-byte header declares the text section immediately
         * before it.  A complete LHA parse still validates the candidate. */
        uint64_t text_size = xx_data_get_u32(h + 2, 4, 0, true);
        if (text_size > (uint64_t)limit - 28U) return false;
        start = 28 + (int64_t)text_size;
        if (sfx_lha_try_at(r, start, limit, pd) || sfx_lha_try_atari_tail(r, start, limit, pd)) return true;
    } else {
        return false;
    }
    if (sfx_lha_scan_range(r, start, limit, pd)) return true;
    /* Some DOS self-extractors place the LHA stream inside their declared
     * image, not at or after the conventional overlay offset.  Probe only
     * the first 64 KiB and require the full inner parser to validate it. */
    return mz_carrier && start != 0 && sfx_lha_scan_range(r, 0, limit, pd);
}

static int64_t sfx_lha_size(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_sfx_lha_handle_base_info(f, pd) ? f->format_size : -1;
}
static uint64_t sfx_lha_count(Abstractformat *f, xx_pd_struct *pd)
{
    return xx_sfx_lha_handle_base_info(f, pd) ? f->number_of_archive_records : 0;
}
static xx_archive_record_state *sfx_lha_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_sfx_lha *r = (xx_sfx_lha *)f;
    if (!sfx_lha_ensure(r, pd)) return NULL;
    return xx_lha_create_archive_records_reading(&r->inner.format, opts, pd);
}
static const xx_archive_record *sfx_lha_current(Abstractformat *f, xx_archive_record_state *state)
{
    return xx_lha_get_current_archive_record(&((xx_sfx_lha *)f)->inner.format, state);
}
static bool sfx_lha_next(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    return xx_lha_archive_record_move_to_next(&((xx_sfx_lha *)f)->inner.format, state, pd);
}
static bool sfx_lha_unpack(Abstractformat *f, xx_archive_record_state *state, xx_pd_struct *pd)
{
    return xx_lha_unpack_current_archive_record(&((xx_sfx_lha *)f)->inner.format, state, pd);
}
static void sfx_lha_free_records(Abstractformat *f, xx_archive_record_state *state)
{
    xx_lha_free_archive_records_reading(&((xx_sfx_lha *)f)->inner.format, state);
}

void xx_sfx_lha_init(xx_sfx_lha *r, xx_io_device *d, int64_t b)
{
    Abstractformat *f;
    if (!r) return;
    xx_mem_zero(r, sizeof(*r));
    f = &r->format;
    xx_format_init(f, d, b);
    f->file_type = XX_FILE_TYPE_SFX_LHA;
    f->format_type = XX_TYPE_ARCHIVE;
    f->is_archive = true;
    xx_format_set_extension(f, "exe");
    f->check_is_valid = xx_sfx_lha_check_is_valid;
    f->handle_base_info = xx_sfx_lha_handle_base_info;
    f->get_format_size = sfx_lha_size;
    f->get_number_of_archive_records = sfx_lha_count;
    f->create_archive_records_reading = sfx_lha_records;
    f->get_current_archive_record = sfx_lha_current;
    f->archive_record_move_to_next = sfx_lha_next;
    f->unpack_current_archive_record = sfx_lha_unpack;
    f->free_archive_records_reading = sfx_lha_free_records;
}
xx_sfx_lha *xx_sfx_lha_create(xx_io_device *d, int64_t b)
{
    xx_sfx_lha *r = (xx_sfx_lha *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sfx_lha_init(r, d, b);
    return r;
}
void xx_sfx_lha_destroy(xx_sfx_lha *r)
{
    if (!r) return;
    if (r->inner_ready) xx_lha_destroy(&r->inner);
    if (r->bounded_device) xx_io_close(r->bounded_device);
    xx_mem_free(r->bounded_bytes);
    xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sfx_lha_free(xx_sfx_lha *r)
{
    if (r) {
        xx_sfx_lha_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sfx_lha_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return f && sfx_lha_ensure((xx_sfx_lha *)f, pd);
}
bool xx_sfx_lha_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    xx_sfx_lha *r = (xx_sfx_lha *)f;
    int64_t limit;
    if (!f || !sfx_lha_ensure(r, pd)) return false;
    limit = sfx_lha_available(f);
    f->format_size = r->payload_offset + r->inner.format.format_size;
    f->number_of_archive_records = r->inner.format.number_of_archive_records;
    f->overlay_size = limit - f->format_size;
    f->overlay_offset = f->overlay_size > 0 ? f->base_address + f->format_size : -1;
    f->is_valid = true;
    f->base_info_handled = true;
    return true;
}
