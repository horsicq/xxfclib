/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * XPAK: one 25-byte header and one packed stream.  xx_xpak.h carries the
 * field table and the evidence.
 *
 * The container is fully described; the CODEC IS NOT.  Every corpus stream
 * opens on the same ten bytes and carries 7.94 to 7.99 bits of byte entropy,
 * so it is an entropy coder, and neither XArchive nor U3 has a handler for
 * it.  Members are listed with their real name and both real sizes and
 * unpack refuses, which is worth more than a decoder that emits garbage.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xpak/xx_xpak.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as XPAK is registered there. */
#ifdef XPAK
#define XX_XPAK_FILE_TYPE XX_FILE_TYPE_XPAK
#else
#define XX_XPAK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XPAK_HEADER_SIZE 25
#define XPAK_NAME_OFFSET 8
#define XPAK_SIZE_OFFSET 21
/* The unpacked size is a u32, so this is the widest member the container can
 * describe at all; the cap only rules out a field that is plainly garbage. */
#define XPAK_MAX_UNPACKED INT64_C(0x10000000)

typedef struct xpak_context_s {
    char name[XX_XPAK_NAME_FIELD + 1];
    int64_t input_size;
    int64_t declared_size;
    int64_t archive_size;  /**< Clamped to what is actually present. */
    int64_t stream_offset;
    int64_t stream_size;
    int64_t unpacked_size;
    bool truncated;
} xpak_context;

typedef struct xpak_stream_s {
    xpak_context context;
    size_t index;
    size_t count;
} xpak_stream;

static uint32_t xpak_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool xpak_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* The name field is read at its FIELD size, never size - 1.  A name has at
 * least one printable byte, is terminated inside the field, and everything
 * behind the terminator is zero: stale bytes there would mean this is not an
 * XPAK header at all. */
static bool xpak_decode_name(const uint8_t *field, char *out) {
    int32_t terminator = -1, index;
    for (index = 0; index < XX_XPAK_NAME_FIELD; ++index) {
        if (field[index] == 0U) {
            terminator = index;
            break;
        }
        if (field[index] < 0x20U || field[index] > 0x7eU ||
            field[index] == '/' || field[index] == '\\' ||
            field[index] == ':') return false;
        out[index] = (char)field[index];
    }
    if (terminator < 1) return false;
    for (index = terminator; index < XX_XPAK_NAME_FIELD; ++index)
        if (field[index] != 0U) return false;
    out[terminator] = 0;
    return true;
}

static void xpak_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool xpak_parse(Abstractformat *format, xpak_context *out) {
    uint8_t header[XPAK_HEADER_SIZE];
    xpak_context context;
    int64_t total, size;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)XPAK_HEADER_SIZE ||
        !xpak_read_at(format->device, format->base_address, header,
                      sizeof(header))) return false;
    if (xx_rt_memcmp(header, "XPAK", 4U) != 0) return false;
    xx_mem_zero(&context, sizeof(context));
    if (!xpak_decode_name(header + XPAK_NAME_OFFSET, context.name))
        return false;
    context.input_size = size;
    context.declared_size = (int64_t)xpak_le32(header + 4U);
    context.unpacked_size = (int64_t)xpak_le32(header + XPAK_SIZE_OFFSET);
    /* The archive size counts this header, so anything at or below it
     * describes no member at all. */
    if (context.declared_size <= (int64_t)XPAK_HEADER_SIZE) return false;
    if (context.unpacked_size <= 0 ||
        context.unpacked_size > XPAK_MAX_UNPACKED) return false;
    /* A declared size longer than the file is a truncated archive, not a
     * wrong one: the header is intact and the member is still named and
     * measured.  The extent published is what is actually present. */
    context.truncated = context.declared_size > size;
    context.archive_size = context.truncated ? size : context.declared_size;
    context.stream_offset = format->base_address + XPAK_HEADER_SIZE;
    context.stream_size = context.archive_size - XPAK_HEADER_SIZE;
    if (context.stream_size <= 0) return false;
    *out = context;
    return true;
}

static bool xpak_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool xpak_set_record(xx_archive_record *record,
                            const xpak_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->stream_offset - XPAK_HEADER_SIZE;
    record->header_size = XPAK_HEADER_SIZE;
    record->data_offset = context->stream_offset;
    record->compressed_size = context->stream_size;
    return xx_archive_record_set_original_name(record, context->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_xpak_init(xx_xpak *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_XPAK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-xpak");
    xx_format_set_extension(&archive->format, "xpak");
    archive->format.check_is_valid = xx_xpak_check_is_valid;
    archive->format.handle_base_info = xx_xpak_handle_base_info;
    archive->format.get_format_size = xx_xpak_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_xpak_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_xpak_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_xpak_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_xpak_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_xpak_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_xpak_free_archive_records_reading;
    archive->declared_size = -1;
}

xx_xpak *xx_xpak_create(xx_io_device *device, int64_t base_address) {
    xx_xpak *archive = (xx_xpak *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_xpak_init(archive, device, base_address);
    return archive;
}

void xx_xpak_destroy(xx_xpak *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_xpak_free(xx_xpak *archive) {
    if (!archive) return;
    xx_xpak_destroy(archive);
    xx_mem_free(archive);
}

bool xx_xpak_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xpak_context context;
    (void)pd;
    return xpak_parse(format, &context);
}

bool xx_xpak_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xpak_context context;
    xx_xpak *archive;
    (void)pd;
    if (!format || !xpak_parse(format, &context)) return false;
    archive = (xx_xpak *)format;
    archive->number_of_records = 1U;
    archive->unpacked_size = (uint64_t)context.unpacked_size;
    archive->declared_size = context.declared_size;
    archive->truncated = context.truncated;
    format->number_of_archive_records = 1U;
    format->format_size = context.archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_xpak_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xpak_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_xpak_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xpak_handle_base_info(format, pd))
               ? ((xx_xpak *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_xpak_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xpak_stream *stream;
    xx_archive_record_state *state;
    xpak_context context;
    (void)pd;
    if (!xpak_parse(format, &context)) return NULL;
    stream = (xpak_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->context = context;
    stream->count = 1U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xpak_stream_free;
    state->total_records = 1U;
    if (!xpak_copy_options(&state->options, options) ||
        !xpak_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_xpak_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_xpak_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    xpak_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (xpak_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

bool xx_xpak_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    /* The codec behind the stream has not been identified, so there is
     * nothing honest to write here.  Refusing beats emitting garbage. */
    (void)format;
    (void)state;
    (void)pd;
    return false;
}

void xx_xpak_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
