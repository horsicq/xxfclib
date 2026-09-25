/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Headerless Stac Electronics LZS stream.  The codec is the one described in
 * xx_stac.h (ANSI X3.241 / RFC 2395); the decoder is deliberately duplicated
 * here rather than shared with the "sTaC" reader so that neither module can
 * fail to build or link because the other was not registered yet.
 *
 * Detection is the decode: see xx_rawstac.h for why U3's three hard-coded
 * 16-byte prefixes were not ported as a rule.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rawstac/xx_rawstac.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as RAWSTAC is registered there. */
#ifdef RAWSTAC
#define XX_RAWSTAC_FILE_TYPE XX_FILE_TYPE_RAWSTAC
#else
#define XX_RAWSTAC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The stream carries no name of its own. */
#define RAWSTAC_MEMBER_NAME "rawstac.bin"

typedef struct rawstac_stream_s {
    int64_t packed_size;
    int64_t unpacked_size;
    bool consumed;
} rawstac_stream;

/* ----------------------------------------------------------------- LZS -- */

typedef struct rawstac_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position; /**< in bits */
} rawstac_bits;

static bool rawstac_bit(rawstac_bits *reader, uint32_t *value) {
    if (reader->position >= reader->size * 8U) return false;
    *value = (uint32_t)((reader->data[reader->position >> 3] >>
                         (7U - (reader->position & 7U))) &
                        1U);
    ++reader->position;
    return true;
}

static bool rawstac_bits_read(rawstac_bits *reader, unsigned count,
                              uint32_t *value) {
    uint32_t result = 0U;
    unsigned index;
    for (index = 0U; index < count; ++index) {
        uint32_t bit;
        if (!rawstac_bit(reader, &bit)) return false;
        result = (result << 1U) | bit;
    }
    *value = result;
    return true;
}

/* One LZS pass.  @p output may be NULL to measure only; @p capacity bounds
 * the output in both modes so that no field in the stream can drive an
 * unbounded allocation. */
static bool rawstac_lzs_run(const uint8_t *input, size_t input_size,
                            uint8_t *output, size_t capacity, size_t *written,
                            size_t *consumed_bits) {
    rawstac_bits reader;
    size_t position = 0U;

    reader.data = input;
    reader.size = input_size;
    reader.position = 0U;

    for (;;) {
        uint32_t bit, value, offset, length;

        if (!rawstac_bit(&reader, &bit)) return false;
        if (bit == 0U) {
            if (!rawstac_bits_read(&reader, 8U, &value)) return false;
            if (position >= capacity) return false;
            if (output) output[position] = (uint8_t)value;
            ++position;
            continue;
        }
        if (!rawstac_bit(&reader, &bit)) return false;
        if (bit != 0U) {
            if (!rawstac_bits_read(&reader, 7U, &offset)) return false;
            if (offset == 0U) break; /* end of stream */
        } else {
            if (!rawstac_bits_read(&reader, 11U, &offset)) return false;
            if (offset == 0U) return false;
        }
        if ((size_t)offset > position) return false;

        if (!rawstac_bits_read(&reader, 2U, &value)) return false;
        if (value < 3U) {
            length = 2U + value;
        } else {
            if (!rawstac_bits_read(&reader, 2U, &value)) return false;
            if (value < 3U) {
                length = 5U + value;
            } else {
                if (!rawstac_bits_read(&reader, 4U, &value)) return false;
                if (value < 15U) {
                    length = 8U + value;
                } else {
                    length = 23U;
                    for (;;) {
                        if (!rawstac_bits_read(&reader, 4U, &value))
                            return false;
                        length += value;
                        if (value != 15U) break;
                        if ((size_t)length > capacity) return false;
                    }
                }
            }
        }
        if ((size_t)length > capacity - position) return false;
        if (output) {
            size_t index;
            for (index = 0U; index < (size_t)length; ++index) {
                output[position + index] =
                    output[position + index - (size_t)offset];
            }
        }
        position += (size_t)length;
    }

    if (written) *written = position;
    if (consumed_bits) *consumed_bits = reader.position;
    return true;
}

/* ------------------------------------------------------------- helpers -- */

static bool rawstac_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static void rawstac_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static uint8_t *rawstac_load_packed(Abstractformat *format, int64_t offset,
                                    int64_t size) {
    uint8_t *packed;
    /* A real byte ceiling, never SIZE_MAX: on a 64-bit build (int64_t)SIZE_MAX
     * is -1 and would reject every size. */
    if (size <= 0 || size > XX_RAWSTAC_MAX_PACKED_SIZE) return NULL;
    packed = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!packed) return NULL;
    if (!rawstac_read_at(format->device, offset, packed, (size_t)size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

/* --------------------------------------------------------------- parse -- */

static bool rawstac_parse(Abstractformat *format, rawstac_stream **result,
                          xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    rawstac_stream *stream = NULL;
    int64_t total, span;
    size_t written = 0U, consumed_bits = 0U;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    /* Short bit strings match any grammar by accident, and an oversized one
     * is not worth decoding speculatively. */
    if (span < XX_RAWSTAC_MIN_PACKED_SIZE || span > XX_RAWSTAC_MAX_PACKED_SIZE)
        return false;

    packed = rawstac_load_packed(format, format->base_address, span);
    if (!packed) return false;

    /* Measuring pass: nothing is allocated for output until the stream has
     * proved it really is LZS. */
    if (!rawstac_lzs_run(packed, (size_t)span, NULL,
                         (size_t)XX_RAWSTAC_MAX_DECODED_SIZE, &written,
                         &consumed_bits))
        goto fail;
    /* The whole file must be the stream: the end marker has to land in the
     * last byte.  Without a header this is the only self-description the
     * file has, and it is what keeps arbitrary data from matching. */
    if ((int64_t)((consumed_bits + 7U) / 8U) != span) goto fail;
    /* It must be a compression, not an expansion of noise. */
    if ((int64_t)written < XX_RAWSTAC_MIN_DECODED_SIZE ||
        (int64_t)written < span)
        goto fail;

    stream = (rawstac_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;
    stream->packed_size = span;
    stream->unpacked_size = (int64_t)written;
    stream->consumed = false;

    xx_mem_free(packed);
    *result = stream;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (stream) xx_mem_free(stream);
    return false;
}

/* -------------------------------------------------------------- record -- */

static bool rawstac_copy_options(xx_list_s *destination,
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

static const xx_var *rawstac_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool rawstac_set_record(xx_archive_record *record,
                               const Abstractformat *format,
                               const rawstac_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = 0;
    record->data_offset = format->base_address;
    record->compressed_size = stream->packed_size;
    return xx_archive_record_set_original_name(record, RAWSTAC_MEMBER_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)stream->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_rawstac_init(xx_rawstac *archive, xx_io_device *device,
                     int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG; /* LZS reads bits MSB first. */
    archive->format.file_type = XX_RAWSTAC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-stac-lzs");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_rawstac_check_is_valid;
    archive->format.handle_base_info = xx_rawstac_handle_base_info;
    archive->format.get_format_size = xx_rawstac_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rawstac_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rawstac_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rawstac_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rawstac_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rawstac_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rawstac_free_archive_records_reading;
    archive->packed_size = -1;
    archive->unpacked_size = -1;
}

xx_rawstac *xx_rawstac_create(xx_io_device *device, int64_t base_address) {
    xx_rawstac *archive = (xx_rawstac *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rawstac_init(archive, device, base_address);
    return archive;
}

void xx_rawstac_destroy(xx_rawstac *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_rawstac_free(xx_rawstac *archive) {
    if (!archive) return;
    xx_rawstac_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_rawstac_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    rawstac_stream *stream;
    if (!rawstac_parse(format, &stream, pd)) return false;
    rawstac_stream_free(stream);
    return true;
}

bool xx_rawstac_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    rawstac_stream *stream;
    xx_rawstac *archive;

    if (!format || !rawstac_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_rawstac *)format;
    archive->packed_size = stream->packed_size;
    archive->unpacked_size = stream->unpacked_size;
    format->number_of_archive_records = 1U;
    /* The stream is verified to run to the last byte, so there is no overlay
     * to report. */
    format->format_size = stream->packed_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    rawstac_stream_free(stream);
    return true;
}

int64_t xx_rawstac_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rawstac_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_rawstac_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rawstac_handle_base_info(format, pd))
               ? 1U
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_rawstac_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    rawstac_stream *stream;
    xx_archive_record_state *state;

    if (!rawstac_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        rawstac_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rawstac_stream_free;
    state->total_records = 1;
    if (!rawstac_copy_options(&state->options, options) ||
        !rawstac_set_record(&state->current_record, format, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_rawstac_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_rawstac_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    rawstac_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (rawstac_stream *)state->internal_state)) {
        if (state) state->has_record = false;
        return false;
    }
    stream->consumed = true;
    ++state->current_index;
    state->has_record = false;
    return false;
}

bool xx_rawstac_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    rawstac_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t done = 0U;
    bool result = false;
    bool created = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (rawstac_stream *)state->internal_state) ||
        stream->consumed || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (stream->unpacked_size <= 0 ||
        stream->unpacked_size > XX_RAWSTAC_MAX_DECODED_SIZE)
        return false;

    packed = rawstac_load_packed(format, format->base_address,
                                 stream->packed_size);
    if (!packed) goto done;
    plain = (uint8_t *)xx_mem_alloc((size_t)stream->unpacked_size);
    if (!plain) goto done;
    if (!rawstac_lzs_run(packed, (size_t)stream->packed_size, plain,
                         (size_t)stream->unpacked_size, &written, NULL))
        goto done;
    if (written != (size_t)stream->unpacked_size) goto done;

    path_option = rawstac_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", RAWSTAC_MEMBER_NAME)
               : xx_str_concat(base, RAWSTAC_MEMBER_NAME);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (done < written) {
            ssize_t amount =
                xx_io_write(destination, plain + done, written - done);
            if (amount <= 0 || (size_t)amount > written - done) {
                result = false;
                break;
            }
            done += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_rawstac_free_archive_records_reading(Abstractformat *format,
                                             xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

int64_t xx_rawstac_get_packed_size(const xx_rawstac *archive) {
    return archive ? archive->packed_size : -1;
}

int64_t xx_rawstac_get_unpacked_size(const xx_rawstac *archive) {
    return archive ? archive->unpacked_size : -1;
}
