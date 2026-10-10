/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LIF: the "DC"/"DL" single-member compressed-file container.  The
 * header test is the recovered recognition predicate (FUN_00555840), tightened
 * with the two structural identities that hold in the whole corpus.  The
 * Method 6 is Zoo LZD with an explicit EOF code.  Its decoded size is
 * measured by scanning the code stream before the record is published.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lif/xx_lif.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/zoo/xx_zoo.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/data/xx_data.h"

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as LIF is registered there. */
#ifdef LIF
#define XX_LIF_FILE_TYPE XX_FILE_TYPE_LIF
#else
#define XX_LIF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct lif_stream_s {
    char name[XX_LIF_NAME_SIZE + 1];
    int64_t packed_offset;
    int64_t packed_size;
    size_t stream_size;
    size_t unpacked_size;
    int64_t total_size;
    uint16_t dos_date;
    uint16_t dos_time;
    bool consumed;
} lif_stream;

#define LIF_MAX_PACKED (64U * 1024U * 1024U)
#define LIF_MAX_PLAIN (256U * 1024U * 1024U)
#define LIF_LZD_TABLE_SIZE 8192U

/* Zoo LZD's low-bit-first codes make the EOF byte position and decoded size
 * recoverable without constructing the output.  The ordinary ZOO decoder
 * still validates the full expansion during unpacking. */
static bool lif_scan_lzd(const uint8_t *data, size_t size,
                         size_t *stream_size, size_t *unpacked_size) {
    uint32_t lengths[LIF_LZD_TABLE_SIZE] = {0};
    size_t bit = 0U, output = 0U;
    uint32_t next_code = 258U, width_limit = 512U;
    int32_t previous = -1;
    unsigned width = 9U;
    bool initial_clear = false;
    uint32_t i;
    if (!data || !stream_size || !unpacked_size || size < 3U ||
        size > LIF_MAX_PACKED)
        return false;
    for (i = 0U; i < 256U; ++i) lengths[i] = 1U;
    for (;;) {
        uint32_t code = 0U, length;
        unsigned j;
        if (bit > size * 8U || width > size * 8U - bit) return false;
        for (j = 0U; j < width; ++j) {
            size_t position = bit + j;
            code |= (uint32_t)((data[position >> 3U] >> (position & 7U)) &
                               1U)
                    << j;
        }
        bit += width;
        if (!initial_clear) {
            if (code != 256U) return false;
            initial_clear = true;
        }
        if (code == 257U) break;
        if (code == 256U) {
            width = 9U;
            width_limit = 512U;
            next_code = 258U;
            previous = -1;
            continue;
        }
        if (previous < 0 && code >= 256U) return false;
        if (next_code >= LIF_LZD_TABLE_SIZE) return false;
        if (code < next_code && lengths[code] != 0U)
            length = lengths[code];
        else if (previous >= 0 && code == next_code)
            length = lengths[previous] + 1U;
        else
            return false;
        if (length > LIF_MAX_PLAIN - output) return false;
        output += length;
        if (previous >= 0) {
            lengths[next_code++] = lengths[previous] + 1U;
            if (next_code >= width_limit && width < 13U) {
                ++width;
                width_limit <<= 1U;
            }
        }
        previous = (int32_t)code;
    }
    if (output == 0U) return false;
    *stream_size = (bit + 7U) / 8U;
    if (size - *stream_size > 1U ||
        (size > *stream_size && data[*stream_size] != 0U) ||
        ((bit & 7U) != 0U &&
         (data[*stream_size - 1U] >> (bit & 7U)) != 0U))
        return false;
    *unpacked_size = output;
    return true;
}

static bool lif_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* The member name reaches a caller as an output file name, so every
 * separator and both dot-only shapes have to die here. */
static bool lif_copy_name(const uint8_t *field, char *out) {
    size_t length = 0U, index;
    while (length < XX_LIF_NAME_SIZE && field[length] != 0U) ++length;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t value = field[index];
        if (value < 0x20U || value >= 0x7fU) return false;
        if (value == '/' || value == '\\' || value == ':' || value == '<' ||
            value == '>' || value == '"' || value == '|' || value == '?' ||
            value == '*')
            return false;
        out[index] = (char)value;
    }
    out[length] = 0;
    if (out[0] == '.' && (length == 1U || (length == 2U && out[1] == '.')))
        return false;
    return true;
}

static void lif_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

/* --------------------------------------------------------------- parse -- */

static bool lif_parse(Abstractformat *format, lif_stream **result,
                      xx_pd_struct *pd) {
    uint8_t header[XX_LIF_HEADER_SIZE];
    lif_stream *stream;
    uint8_t *packed = NULL;
    size_t stream_size, unpacked_size;
    int64_t total, span;
    uint32_t declared_total, method, packed_size;
    uint16_t magic;

    if (!format || !format->device || !result || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    span = total - format->base_address;
    if (span <= (int64_t)XX_LIF_HEADER_SIZE) return false;
    if (!lif_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;

    magic = xx_data_get_u16(header, 2, 0, false);
    if (magic != 0x4344U && magic != 0x4c44U) return false;
    if (xx_data_get_u16(header + 2, 2, 0, false) != XX_LIF_VERSION) return false;
    if (xx_data_get_u16(header + 4, 2, 0, false) == 0U) return false;

    declared_total = xx_data_get_u32(header + 0x15, 4, 0, false);
    method = xx_data_get_u32(header + 0x19, 4, 0, false);
    packed_size = xx_data_get_u32(header + 0x1d, 4, 0, false);
    if (declared_total == 0U || (declared_total & 0x80000000U) != 0U)
        return false;
    if (method != XX_LIF_METHOD) return false;
    if ((packed_size & 0x80000000U) != 0U ||
        packed_size > LIF_MAX_PACKED)
        return false;
    if (xx_data_get_u32(header + 0x21, 4, 0, false) != 0U) return false;

    /* The two structural identities.  A two-byte magic is far too weak on its
     * own, and these are what make it safe: the declared total must cover the
     * whole of what was handed to the reader, and the packed extent must be
     * exactly what is left after the header. */
    if ((int64_t)declared_total != span) return false;
    if ((int64_t)packed_size != span - (int64_t)XX_LIF_HEADER_SIZE)
        return false;

    packed = (uint8_t *)xx_mem_alloc(packed_size != 0U ? packed_size : 1U);
    if (!packed || !lif_read_at(format->device,
                                format->base_address + XX_LIF_HEADER_SIZE,
                                packed, packed_size) ||
        !lif_scan_lzd(packed, packed_size, &stream_size, &unpacked_size)) {
        if (packed) xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);

    stream = (lif_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!lif_copy_name(header + 6, stream->name)) {
        xx_mem_free(stream);
        return false;
    }
    stream->packed_offset = format->base_address + (int64_t)XX_LIF_HEADER_SIZE;
    stream->packed_size = (int64_t)packed_size;
    stream->stream_size = stream_size;
    stream->unpacked_size = unpacked_size;
    stream->total_size = (int64_t)declared_total;
    stream->dos_date = xx_data_get_u16(header + 0x25, 2, 0, false);
    stream->dos_time = xx_data_get_u16(header + 0x27, 2, 0, false);
    stream->consumed = false;
    *result = stream;
    return true;
}

/* -------------------------------------------------------------- record -- */

static bool lif_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool lif_set_record(xx_archive_record *record,
                           const Abstractformat *format,
                           const lif_stream *stream) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address;
    record->header_size = XX_LIF_HEADER_SIZE;
    record->data_offset = stream->packed_offset;
    record->compressed_size = (int64_t)stream->stream_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)stream->stream_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)stream->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          XX_LIF_METHOD) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          stream->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          stream->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_lif_init(xx_lif *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LIF_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/octet-stream");
    xx_format_set_extension(&archive->format, "cmp");
    xx_format_set_version(&archive->format, "2");
    archive->format.check_is_valid = xx_lif_check_is_valid;
    archive->format.handle_base_info = xx_lif_handle_base_info;
    archive->format.get_format_size = xx_lif_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lif_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lif_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lif_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lif_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lif_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lif_free_archive_records_reading;
    archive->packed_offset = -1;
    archive->packed_size = -1;
}

xx_lif *xx_lif_create(xx_io_device *device, int64_t base_address) {
    xx_lif *archive = (xx_lif *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lif_init(archive, device, base_address);
    return archive;
}

void xx_lif_destroy(xx_lif *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_lif_free(xx_lif *archive) {
    if (!archive) return;
    xx_lif_destroy(archive);
    xx_mem_free(archive);
}

/* -------------------------------------------------------------- format -- */

bool xx_lif_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    lif_stream *stream;
    if (!lif_parse(format, &stream, pd)) return false;
    lif_stream_free(stream);
    return true;
}

bool xx_lif_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    lif_stream *stream;
    xx_lif *archive;

    if (!format || !lif_parse(format, &stream, pd)) {
        if (format) {
            format->is_valid = false;
            format->base_info_handled = false;
        }
        return false;
    }
    archive = (xx_lif *)format;
    archive->packed_offset = stream->packed_offset;
    archive->packed_size = stream->packed_size;
    archive->dos_date = stream->dos_date;
    archive->dos_time = stream->dos_time;
    format->number_of_archive_records = 1U;
    /* The declared total was verified to equal what was handed to the reader,
     * so there is no overlay. */
    format->format_size = stream->total_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    lif_stream_free(stream);
    return true;
}

int64_t xx_lif_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_lif_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_lif_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lif_handle_base_info(format, pd))
               ? 1U
               : 0U;
}

/* ------------------------------------------------------ record reading -- */

xx_archive_record_state *xx_lif_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lif_stream *stream;
    xx_archive_record_state *state;

    if (!lif_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lif_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = lif_stream_free;
    state->total_records = 1;
    if (!lif_copy_options(&state->options, options) ||
        !lif_set_record(&state->current_record, format, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_lif_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lif_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    lif_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (lif_stream *)state->internal_state)) {
        if (state) state->has_record = false;
        return false;
    }
    stream->consumed = true;
    ++state->current_index;
    state->has_record = false;
    return false;
}

static const xx_var *lif_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool lif_decode(Abstractformat *format, const lif_stream *stream,
                       uint8_t **result) {
    uint8_t *packed = NULL, *plain = NULL;
    size_t written = 0U;
    if (!format || !stream || !result || stream->stream_size == 0U ||
        stream->unpacked_size == 0U)
        return false;
    packed = (uint8_t *)xx_mem_alloc(stream->stream_size);
    plain = (uint8_t *)xx_mem_alloc(stream->unpacked_size);
    if (!packed || !plain ||
        !lif_read_at(format->device, stream->packed_offset, packed,
                     stream->stream_size) ||
        !xx_zoo_lzd_decode_memory(packed, stream->stream_size, plain,
                                  stream->unpacked_size, &written) ||
        written != stream->unpacked_size) {
        if (packed) xx_mem_free(packed);
        if (plain) xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    *result = plain;
    return true;
}

bool xx_lif_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    lif_stream *stream;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL, *path = NULL;
    uint8_t *plain = NULL;
    xx_io_device *destination = NULL;
    size_t written = 0U;
    bool ok = false, created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (lif_stream *)state->internal_state) || stream->consumed ||
        (pd && xx_pd_is_stopped(pd)) || !lif_decode(format, stream, &plain))
        return false;
    option = lif_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        ok = true; /* A missing destination requests decode verification. */
        goto done;
    }
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING ||
             option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    destination = xx_io_file_open(path, "wb");
    created = destination != NULL;
    if (!destination) goto done;
    ok = true;
    while (written < stream->unpacked_size) {
        size_t chunk = stream->unpacked_size - written;
        ssize_t amount;
        if (chunk > 1024U * 1024U) chunk = 1024U * 1024U;
        if (pd && xx_pd_is_stopped(pd)) {
            ok = false;
            break;
        }
        amount = xx_io_write(destination, plain + written, chunk);
        if (amount <= 0 || (size_t)amount > chunk) {
            ok = false;
            break;
        }
        written += (size_t)amount;
    }
    if (xx_io_close(destination) != 0) ok = false;
    destination = NULL;
    if (!ok && created) xx_rt_remove(path);
done:
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return ok;
}

void xx_lif_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}

/* ----------------------------------------------------------- accessors -- */

int64_t xx_lif_get_packed_offset(const xx_lif *archive) {
    return archive ? archive->packed_offset : -1;
}

int64_t xx_lif_get_packed_size(const xx_lif *archive) {
    return archive ? archive->packed_size : -1;
}
