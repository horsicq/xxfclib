/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "WRZL" single-stream compressed file.  The header test is U3's own
 * recognition predicate (FUN_005710d0), minus the one part of it that could
 * not be recovered - see xx_wrzl.h, which also records why the codec is not
 * implemented and why the record is published anyway.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wrzl/xx_wrzl.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as WRZL is registered there. */
#ifdef WRZL
#define XX_WRZL_FILE_TYPE XX_FILE_TYPE_WRZL
#else
#define XX_WRZL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The container stores no member name. */
#define WRZL_MEMBER_NAME "wrzl.bin"

typedef struct wrzl_stream_s {
    int64_t packed_offset;
    int64_t packed_size;
    int64_t unpacked_size;
    uint16_t opaque_08;
    uint8_t mode;
    uint8_t flags;
    bool consumed;
} wrzl_stream;

static uint16_t wrzl_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t wrzl_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool wrzl_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static void wrzl_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

/* --------------------------------------------------------------- parse -- */

static bool wrzl_parse(Abstractformat *format, wrzl_stream **result,
                       xx_pd_struct *pd) {
    uint8_t header[XX_WRZL_HEADER_SIZE];
    wrzl_stream *stream;
    int64_t total, span;
    uint32_t unpacked_size;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    /* The header plus at least one packed byte. */
    if (span <= (int64_t)XX_WRZL_HEADER_SIZE) return false;
    if (!wrzl_read_at(format->device, format->base_address, header,
                      sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, XX_WRZL_SIGNATURE, XX_WRZL_SIGNATURE_SIZE) != 0)
        return false;

    unpacked_size = wrzl_le32(header + 4);
    /* U3 reads this as a signed int and requires it to be non-negative. */
    if ((unpacked_size & 0x80000000U) != 0U) return false;
    if ((int64_t)unpacked_size > XX_WRZL_MAX_UNCOMPRESSED_SIZE) return false;
    if (wrzl_le16(header + 8) == 0U) return false;
    /* U3's range test on the mode byte.  The 128-bit membership table that
     * refines it could not be recovered from the packed binary, so it is not
     * reproduced; see xx_wrzl.h. */
    if (header[10] < XX_WRZL_MODE_MIN || header[10] > XX_WRZL_MODE_MAX)
        return false;

    stream = (wrzl_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->packed_offset = format->base_address + (int64_t)XX_WRZL_HEADER_SIZE;
    /* The container states no packed length, so it is the measured remainder
     * of what was handed to the reader - never a declared field. */
    stream->packed_size = span - (int64_t)XX_WRZL_HEADER_SIZE;
    stream->unpacked_size = (int64_t)unpacked_size;
    stream->opaque_08 = wrzl_le16(header + 8);
    stream->mode = header[10];
    stream->flags = header[11];
    stream->consumed = false;
    *result = stream;
    return true;
}

/* -------------------------------------------------------------- record -- */

static bool wrzl_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool wrzl_set_record(xx_archive_record *record,
                            const Abstractformat *format,
                            const wrzl_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = XX_WRZL_HEADER_SIZE;
    record->data_offset = stream->packed_offset;
    record->compressed_size = stream->packed_size;
    return xx_archive_record_set_original_name(record, WRZL_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)stream->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          stream->mode) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          stream->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_wrzl_init(xx_wrzl *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_WRZL_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-wrzl");
    xx_format_set_extension(&archive->format, "bml");
    archive->format.check_is_valid = xx_wrzl_check_is_valid;
    archive->format.handle_base_info = xx_wrzl_handle_base_info;
    archive->format.get_format_size = xx_wrzl_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_wrzl_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_wrzl_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_wrzl_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_wrzl_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_wrzl_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_wrzl_free_archive_records_reading;
    archive->packed_offset = -1;
    archive->packed_size = -1;
    archive->unpacked_size = -1;
}

xx_wrzl *xx_wrzl_create(xx_io_device *device, int64_t base_address) {
    xx_wrzl *archive = (xx_wrzl *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_wrzl_init(archive, device, base_address);
    return archive;
}

void xx_wrzl_destroy(xx_wrzl *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_wrzl_free(xx_wrzl *archive) {
    if (!archive) return;
    xx_wrzl_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_wrzl_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    wrzl_stream *stream;
    if (!wrzl_parse(format, &stream, pd)) return false;
    wrzl_stream_free(stream);
    return true;
}

bool xx_wrzl_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    wrzl_stream *stream;
    xx_wrzl *archive;

    if (!format || !wrzl_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_wrzl *)format;
    archive->packed_offset = stream->packed_offset;
    archive->packed_size = stream->packed_size;
    archive->unpacked_size = stream->unpacked_size;
    archive->opaque_08 = stream->opaque_08;
    archive->mode = stream->mode;
    archive->flags = stream->flags;
    format->number_of_archive_records = 1U;
    /* The payload was measured to the end of the device, so there is no
     * overlay to report. */
    format->format_size =
        (int64_t)XX_WRZL_HEADER_SIZE + stream->packed_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    wrzl_stream_free(stream);
    return true;
}

int64_t xx_wrzl_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_wrzl_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_wrzl_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_wrzl_handle_base_info(format, pd))
               ? 1U
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_wrzl_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    wrzl_stream *stream;
    xx_archive_record_state *state;

    if (!wrzl_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        wrzl_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = wrzl_stream_free;
    state->total_records = 1;
    if (!wrzl_copy_options(&state->options, options) ||
        !wrzl_set_record(&state->current_record, format, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_wrzl_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_wrzl_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    wrzl_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (wrzl_stream *)state->internal_state)) {
        if (state) state->has_record = false;
        return false;
    }
    stream->consumed = true;
    ++state->current_index;
    state->has_record = false;
    return false;
}

bool xx_wrzl_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    (void)format;
    (void)state;
    (void)pd;
    /* The codec is not identified.  A reader that refuses is worth more than
     * one that emits garbage, so this fails closed; the packed extent and the
     * stored uncompressed length are still published by the record. */
    return false;
}

void xx_wrzl_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

int64_t xx_wrzl_get_packed_offset(const xx_wrzl *archive) {
    return archive ? archive->packed_offset : -1;
}

int64_t xx_wrzl_get_packed_size(const xx_wrzl *archive) {
    return archive ? archive->packed_size : -1;
}

int64_t xx_wrzl_get_unpacked_size(const xx_wrzl *archive) {
    return archive ? archive->unpacked_size : -1;
}
