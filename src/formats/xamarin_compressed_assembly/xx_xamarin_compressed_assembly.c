/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Xamarin.Android compressed assembly: "XALZ", a descriptor index, the
 * uncompressed size, then one raw LZ4 block.  xx_xamarin_compressed_assembly.h
 * carries the field table.
 *
 * Nothing in the header says where the block ends.  The sequence grammar
 * does: each sequence is a token, its literals and (except for the last) a
 * 16-bit back-reference distance plus a match length.  Walking those lengths
 * -- without producing any output -- until the running output count reaches
 * the declared size exactly, on a literal-only sequence, both validates the
 * stream (every distance must point inside what has been produced, no length
 * may overshoot the declared size) and yields the packed length.  The walk
 * needs only a 64 KiB read window, and because every length is checked against the
 * remaining declared output it reads at most about size/255 bytes of a
 * hostile run of 0xFF extension bytes before refusing it.
 *
 * Extraction decodes the measured block in memory with the library's raw
 * LZ4 block decoder and insists on exactly the declared size.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xamarin_compressed_assembly/xx_xamarin_compressed_assembly.h"

#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef XAMARIN_COMPRESSED_ASSEMBLY
#define XX_XAMARIN_COMPRESSED_ASSEMBLY_FILE_TYPE \
    XX_FILE_TYPE_XAMARIN_COMPRESSED_ASSEMBLY
#else
#define XX_XAMARIN_COMPRESSED_ASSEMBLY_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XCA_HEADER_SIZE 12
/* Largest declared size accepted.  Real assemblies are a few tens of MiB at
 * most; extraction holds the whole output in memory, so this is the bound on
 * that allocation. */
#define XCA_MAX_UNPACKED UINT32_C(0x0FFFFFFF) /* just under 256 MiB */
#define XCA_WINDOW 65536U
#define XCA_PAYLOAD_NAME "assembly.dll"

typedef struct xca_context_s {
    int64_t header_offset;
    int64_t packed_offset;
    int64_t packed_size;
    int64_t format_size;
    uint32_t descriptor_index;
    uint32_t unpacked_size;
} xca_context;

typedef struct xca_stream_s {
    xca_context context;
    size_t index;
    size_t count;
} xca_stream;

static uint32_t xca_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool xca_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Sequential byte reader over [base, base + size) with a read window. */
typedef struct xca_reader_s {
    xx_io_device *device;
    int64_t base;
    int64_t size;
    int64_t position;  /**< Stream position of the next byte. */
    int64_t start;     /**< Stream position of buffer[0]. */
    size_t length;
    uint8_t *buffer;
} xca_reader;

static bool xca_get(xca_reader *reader, uint8_t *out) {
    int64_t offset;
    if (reader->position >= reader->size) return false;
    offset = reader->position - reader->start;
    if (offset < 0 || (uint64_t)offset >= (uint64_t)reader->length) {
        int64_t available = reader->size - reader->position;
        size_t want = available < (int64_t)XCA_WINDOW ? (size_t)available
                                                      : (size_t)XCA_WINDOW;
        reader->length = 0U;
        if (!xca_read_at(reader->device, reader->base + reader->position,
                         reader->buffer, want)) return false;
        reader->start = reader->position;
        reader->length = want;
        offset = 0;
    }
    *out = reader->buffer[(size_t)offset];
    ++reader->position;
    return true;
}

static bool xca_skip(xca_reader *reader, uint64_t count) {
    if (count > (uint64_t)(reader->size - reader->position)) return false;
    reader->position += (int64_t)count;
    return true;
}

/* LZ4 length extension: 255-valued bytes continue, anything else ends.  The
 * sum is refused as soon as it passes `limit`, so a run of 0xFF bytes costs
 * at most limit/255 reads. */
static bool xca_extend(xca_reader *reader, uint64_t *length, uint64_t limit) {
    for (;;) {
        uint8_t byte;
        if (!xca_get(reader, &byte)) return false;
        *length += byte;
        if (*length > limit) return false;
        if (byte != 255U) return true;
    }
}

/* Walk the sequence grammar of a raw LZ4 block that must decode to exactly
 * `unpacked` bytes; on success `*packed` is the block's length. */
static bool xca_measure(xx_io_device *device, int64_t base, int64_t available,
                        uint32_t unpacked, int64_t *packed,
                        xx_pd_struct *pd) {
    xca_reader reader;
    uint64_t produced = 0U, iterations = 0U;
    bool result = false;
    xx_mem_zero(&reader, sizeof(reader));
    reader.device = device;
    reader.base = base;
    reader.size = available;
    reader.buffer = (uint8_t *)xx_mem_alloc(XCA_WINDOW);
    if (!reader.buffer) return false;
    for (;;) {
        uint8_t token, low, high;
        uint64_t literals, match, distance, room;
        if ((++iterations & 0xFFFFU) == 0U && pd && xx_pd_is_stopped(pd))
            goto done;
        if (!xca_get(&reader, &token)) goto done;
        room = (uint64_t)unpacked - produced;
        literals = (uint64_t)(token >> 4U);
        if (literals == 15U && !xca_extend(&reader, &literals, room)) goto done;
        if (literals > room || !xca_skip(&reader, literals)) goto done;
        produced += literals;
        if (produced == (uint64_t)unpacked) break;
        if (!xca_get(&reader, &low) || !xca_get(&reader, &high)) goto done;
        distance = (uint64_t)low | ((uint64_t)high << 8U);
        if (distance == 0U || distance > produced) goto done;
        room = (uint64_t)unpacked - produced;
        if (room < 4U) goto done;
        match = (uint64_t)(token & 15U);
        if (match == 15U && !xca_extend(&reader, &match, room - 4U)) goto done;
        match += 4U;
        if (match > room) goto done;
        produced += match;
        /* A block always ends on a literal-only sequence, so reaching the
         * size here still needs one more (empty) token. */
    }
    *packed = reader.position;
    result = true;
done:
    xx_mem_free(reader.buffer);
    return result;
}

static bool xca_parse(Abstractformat *format, xca_context *out,
                      xx_pd_struct *pd) {
    uint8_t header[XCA_HEADER_SIZE];
    xca_context context;
    int64_t total, size;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* Header plus at least the one token a non-empty block needs. */
    if (size < XCA_HEADER_SIZE + 1 ||
        !xca_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        header[0] != 'X' || header[1] != 'A' || header[2] != 'L' ||
        header[3] != 'Z') return false;
    xx_mem_zero(&context, sizeof(context));
    context.descriptor_index = xca_le32(header + 4);
    context.unpacked_size = xca_le32(header + 8);
    if (context.unpacked_size == 0U ||
        context.unpacked_size > XCA_MAX_UNPACKED) return false;
    context.header_offset = format->base_address;
    context.packed_offset = format->base_address + XCA_HEADER_SIZE;
    if (!xca_measure(format->device, context.packed_offset,
                     size - XCA_HEADER_SIZE, context.unpacked_size,
                     &context.packed_size, pd)) return false;
    context.format_size = XCA_HEADER_SIZE + context.packed_size;
    *out = context;
    return true;
}

static bool xca_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *xca_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool xca_set_record(xx_archive_record *record,
                           const xca_context *context) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->header_offset;
    record->header_size = XCA_HEADER_SIZE;
    record->data_offset = context->packed_offset;
    record->compressed_size = context->packed_size;
    return xx_archive_record_set_original_name(record, XCA_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)context->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          context->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_xamarin_compressed_assembly_init(
    xx_xamarin_compressed_assembly *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_XAMARIN_COMPRESSED_ASSEMBLY_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "dll");
    archive->format.check_is_valid =
        xx_xamarin_compressed_assembly_check_is_valid;
    archive->format.handle_base_info =
        xx_xamarin_compressed_assembly_handle_base_info;
    archive->format.get_format_size =
        xx_xamarin_compressed_assembly_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_xamarin_compressed_assembly_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_xamarin_compressed_assembly_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_xamarin_compressed_assembly_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_xamarin_compressed_assembly_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_xamarin_compressed_assembly_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_xamarin_compressed_assembly_free_archive_records_reading;
}

xx_xamarin_compressed_assembly *xx_xamarin_compressed_assembly_create(
    xx_io_device *device, int64_t base_address) {
    xx_xamarin_compressed_assembly *archive =
        (xx_xamarin_compressed_assembly *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_xamarin_compressed_assembly_init(archive, device, base_address);
    return archive;
}

void xx_xamarin_compressed_assembly_destroy(
    xx_xamarin_compressed_assembly *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_xamarin_compressed_assembly_free(
    xx_xamarin_compressed_assembly *archive) {
    if (!archive) return;
    xx_xamarin_compressed_assembly_destroy(archive);
    xx_mem_free(archive);
}

bool xx_xamarin_compressed_assembly_check_is_valid(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    xca_context context;
    return xca_parse(format, &context, pd);
}

bool xx_xamarin_compressed_assembly_handle_base_info(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    xca_context context;
    xx_xamarin_compressed_assembly *archive;
    if (!format || !xca_parse(format, &context, pd)) return false;
    archive = (xx_xamarin_compressed_assembly *)format;
    archive->number_of_records = 1U;
    archive->descriptor_index = context.descriptor_index;
    archive->unpacked_size = context.unpacked_size;
    archive->packed_size = context.packed_size;
    format->number_of_archive_records = 1U;
    format->format_size = context.format_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_xamarin_compressed_assembly_get_format_size(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xamarin_compressed_assembly_handle_base_info(format,
                                                                      pd))
               ? format->format_size : -1;
}

uint64_t xx_xamarin_compressed_assembly_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_xamarin_compressed_assembly_handle_base_info(format,
                                                                      pd))
               ? ((xx_xamarin_compressed_assembly *)format)->number_of_records
               : 0U;
}

static void xca_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

xx_archive_record_state *
xx_xamarin_compressed_assembly_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xca_stream *stream;
    xx_archive_record_state *state;
    xca_context context;
    if (!xca_parse(format, &context, pd)) return NULL;
    stream = (xca_stream *)xx_mem_calloc(1U, sizeof(*stream));
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
    state->free_internal = xca_stream_free;
    state->total_records = 1U;
    if (!xca_copy_options(&state->options, options) ||
        !xca_set_record(&state->current_record, &stream->context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *
xx_xamarin_compressed_assembly_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_xamarin_compressed_assembly_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    xca_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (xca_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    return false;
}

/* Decode the measured block into a buffer of exactly the declared size.
 * The caller frees *out_data. */
static bool xca_decode(Abstractformat *format, const xca_context *context,
                       uint8_t **out_data) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool result = false;
    *out_data = NULL;
    if (context->packed_size <= 0 ||
        (uint64_t)context->packed_size > (uint64_t)SIZE_MAX ||
        context->unpacked_size == 0U ||
        context->unpacked_size > XCA_MAX_UNPACKED) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)context->packed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)context->unpacked_size);
    if (!packed || !plain ||
        !xca_read_at(format->device, context->packed_offset, packed,
                     (size_t)context->packed_size) ||
        !xx_lz4_decompress_block(packed, (size_t)context->packed_size, plain,
                                 (size_t)context->unpacked_size, &written) ||
        written != (size_t)context->unpacked_size) goto done;
    *out_data = plain;
    plain = NULL;
    result = true;
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

bool xx_xamarin_compressed_assembly_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    xca_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (xca_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    path_option = xca_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: prove the block decodes and report that. */
        result = xca_decode(format, &stream->context, &plain);
        if (plain) xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    /* Decode first so a bad stream never touches the destination. */
    if (!xca_decode(format, &stream->context, &plain)) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", XCA_PAYLOAD_NAME)
               : xx_str_concat(base, XCA_PAYLOAD_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        size_t done_bytes = 0U, total = (size_t)stream->context.unpacked_size;
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = true;
        while (done_bytes < total) {
            ssize_t amount = xx_io_write(destination, plain + done_bytes,
                                         total - done_bytes);
            if (amount <= 0 || (size_t)amount > total - done_bytes) {
                result = false;
                break;
            }
            done_bytes += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_xamarin_compressed_assembly_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
