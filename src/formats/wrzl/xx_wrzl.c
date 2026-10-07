/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * WRZL wraps independently compressed LZRW1/KH blocks. The algorithm was
 * published by Kurt Haenen in the SWAG Pascal archive (ARCHIVES/0041.PAS);
 * the block rules were checked against all six local corpus files. The
 * selected DUMMY.DA$ output matches U3 byte-for-byte.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wrzl/xx_wrzl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"

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
#define WRZL_BLOCK_PLAIN 32768U
#define WRZL_BLOCK_PACKED 65535U
#define WRZL_MODE_COMPRESSED 0x40U
#define WRZL_MODE_COPIED 0x80U

typedef struct wrzl_stream_s {
    int64_t packed_offset;
    int64_t packed_size;
    int64_t unpacked_size;
    uint16_t opaque_08;
    uint8_t mode;
    uint8_t flags;
    bool consumed;
} wrzl_stream;

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

/* A block begins with 0x40 (coded) or 0x80 (verbatim). In coded blocks each
 * 16-bit big-endian control word describes up to sixteen tokens, highest bit
 * first. A set bit selects a 12-bit distance plus 4-bit match length; zero
 * distance instead selects a four-byte repeated-byte run. */
static bool wrzl_decode_block(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t expected) {
    size_t input_pos = 3U, output_pos = 0U;
    uint32_t command;
    unsigned bits = 16U;
    if (!input || !output || input_size == 0U ||
        expected > WRZL_BLOCK_PLAIN)
        return false;
    if (input[0] == WRZL_MODE_COPIED) {
        if (input_size != expected + 1U) return false;
        xx_rt_memcpy(output, input + 1U, expected);
        return true;
    }
    if (input[0] != WRZL_MODE_COMPRESSED || input_size < 3U)
        return false;
    command = ((uint32_t)input[1] << 8U) | input[2];
    while (input_pos < input_size) {
        if (bits == 0U) {
            if (input_size - input_pos < 2U) return false;
            command = ((uint32_t)input[input_pos] << 8U) |
                      input[input_pos + 1U];
            input_pos += 2U;
            bits = 16U;
            if (input_pos == input_size) return false;
        }
        if ((command & 0x8000U) != 0U) {
            uint32_t distance, length;
            if (input_size - input_pos < 2U) return false;
            distance = ((uint32_t)input[input_pos] << 4U) |
                       (input[input_pos + 1U] >> 4U);
            if (distance == 0U) {
                if (input_size - input_pos < 4U) return false;
                length = ((uint32_t)input[input_pos + 1U] << 8U) |
                         input[input_pos + 2U];
                length += 16U;
                if (length > expected - output_pos) return false;
                xx_rt_memset(output + output_pos, input[input_pos + 3U],
                             length);
                input_pos += 4U;
                output_pos += length;
            } else {
                uint32_t i;
                length = (input[input_pos + 1U] & 0x0fU) + 3U;
                if (distance > output_pos ||
                    length > expected - output_pos)
                    return false;
                for (i = 0U; i < length; ++i) {
                    output[output_pos] = output[output_pos - distance];
                    ++output_pos;
                }
                input_pos += 2U;
            }
        } else {
            if (output_pos >= expected) return false;
            output[output_pos++] = input[input_pos++];
        }
        command = (command << 1U) & 0xffffU;
        --bits;
    }
    return output_pos == expected;
}

static bool wrzl_scan_chunks(Abstractformat *format, int64_t span,
                              uint32_t raw_size, uint16_t *first_length,
                              uint8_t *first_mode, uint8_t *first_control) {
    int64_t cursor = 8;
    uint32_t left = raw_size;
    bool first = true;
    if (left == 0U || span < 11) return false;
    while (left != 0U) {
        uint8_t lead[3];
        uint16_t packed;
        uint32_t plain = left < WRZL_BLOCK_PLAIN ? left : WRZL_BLOCK_PLAIN;
        if (span - cursor < 3 ||
            !wrzl_read_at(format->device, format->base_address + cursor,
                          lead, sizeof(lead)))
            return false;
        packed = xx_data_get_u16(lead, 2, 0, false);
        if (packed == 0U || packed > span - cursor - 2 ||
            (lead[2] != WRZL_MODE_COMPRESSED &&
             lead[2] != WRZL_MODE_COPIED) ||
            (lead[2] == WRZL_MODE_COMPRESSED && packed < 3U) ||
            (lead[2] == WRZL_MODE_COPIED &&
             packed != plain + 1U))
            return false;
        if (first) {
            uint8_t control = 0U;
            if (packed >= 2U &&
                !wrzl_read_at(format->device,
                              format->base_address + cursor + 3,
                              &control, 1U))
                return false;
            *first_length = packed;
            *first_mode = lead[2];
            *first_control = control;
            first = false;
        }
        cursor += (int64_t)packed + 2;
        left -= plain;
    }
    return cursor == span;
}

static bool wrzl_decode_stream(Abstractformat *format,
                                const wrzl_stream *stream,
                                xx_io_device *destination,
                                xx_pd_struct *pd) {
    uint8_t *packed = NULL, *plain = NULL;
    int64_t cursor, end;
    uint64_t left;
    bool ok = false;
    if (!format || !stream || stream->packed_offset < 0 ||
        stream->packed_size < 3 || stream->unpacked_size <= 0)
        return false;
    packed = (uint8_t *)xx_mem_alloc(WRZL_BLOCK_PACKED);
    plain = (uint8_t *)xx_mem_alloc(WRZL_BLOCK_PLAIN);
    if (!packed || !plain) goto done;
    cursor = stream->packed_offset;
    end = cursor + stream->packed_size;
    left = (uint64_t)stream->unpacked_size;
    while (left != 0U) {
        uint8_t size_field[2];
        uint16_t packed_size;
        size_t expected, written = 0U;
        if ((pd && xx_pd_is_stopped(pd)) || end - cursor < 3 ||
            !wrzl_read_at(format->device, cursor, size_field, 2U))
            goto done;
        packed_size = xx_data_get_u16(size_field, 2, 0, false);
        expected = left < WRZL_BLOCK_PLAIN ? (size_t)left :
                                             WRZL_BLOCK_PLAIN;
        cursor += 2;
        if (packed_size == 0U || packed_size > end - cursor ||
            !wrzl_read_at(format->device, cursor, packed, packed_size) ||
            !wrzl_decode_block(packed, packed_size, plain, expected))
            goto done;
        while (destination && written < expected) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         expected - written);
            if (amount <= 0 || (size_t)amount > expected - written)
                goto done;
            written += (size_t)amount;
        }
        cursor += packed_size;
        left -= expected;
    }
    ok = cursor == end;
done:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return ok;
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
    uint16_t first_length;
    uint8_t first_mode, first_control;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    if (span < 11) return false;
    if (!wrzl_read_at(format->device, format->base_address, header,
                      sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, XX_WRZL_SIGNATURE, XX_WRZL_SIGNATURE_SIZE) != 0)
        return false;

    unpacked_size = xx_data_get_u32(header + 4, 4, 0, false);
    /* U3 reads this as a signed int and requires it to be non-negative. */
    if ((unpacked_size & 0x80000000U) != 0U) return false;
    if ((int64_t)unpacked_size > XX_WRZL_MAX_UNCOMPRESSED_SIZE) return false;
    if (!wrzl_scan_chunks(format, span, unpacked_size, &first_length,
                          &first_mode, &first_control))
        return false;

    stream = (wrzl_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->packed_offset = format->base_address +
                            (int64_t)XX_WRZL_HEADER_SIZE;
    /* This region includes every two-byte block length and coded block. */
    stream->packed_size = span - (int64_t)XX_WRZL_HEADER_SIZE;
    stream->unpacked_size = (int64_t)unpacked_size;
    stream->opaque_08 = first_length;
    stream->mode = first_mode;
    stream->flags = first_control;
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
    wrzl_stream *stream;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL;
    xx_io_device *destination = NULL;
    bool ok = false, created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (wrzl_stream *)state->internal_state) ||
        stream->consumed || (pd && xx_pd_is_stopped(pd)))
        return false;
    option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)stream->unpacked_size > xx_var_get_u64(option))
        return false;
    option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return wrzl_decode_stream(format, stream, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING ||
             option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = base[0] ? xx_str_concat3(base, "/", WRZL_MEMBER_NAME) :
                     xx_str_dup(WRZL_MEMBER_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_OVERWRITE);
    if ((!option || !xx_var_get_bool(option)) &&
        xx_io_file_exists_a(path))
        goto done;
    destination = xx_io_file_open(path, "wb");
    if (!destination) goto done;
    created = true;
    ok = wrzl_decode_stream(format, stream, destination, pd);
done:
    if (destination && xx_io_close(destination) != 0) ok = false;
    if (!ok && created && path) xx_io_file_remove_a(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
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
