/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Eschalon Setup ARCV 1.10 reader.  The layout is ported from XArchive's
 * core/xlegacystorearchive.cpp (the FT_ARCV branch): a fixed archive header
 * carrying the single member's name and sizes, immediately followed by one
 * "CHNK" segment whose body is the packed stream.
 *
 * Two LZHUF sub-variants share the container.  The header JAMCRC identifies
 * them: when it matches the packed bytes the member uses the compact F=32
 * bitstream, otherwise the stock F=60 bitstream is trial decoded and both the
 * output length and its JAMCRC must match the header before the archive is
 * accepted.  A file that satisfies neither reading is rejected outright.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arcv/xx_arcv.h"

#include "xxfclib/algo/arcv2/xx_arcv2_lzhuf.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Self-healing file-type shim: the enum entry is added by the coordinator. */
#ifdef ARCV
#define XX_ARCV_FILE_TYPE XX_FILE_TYPE_ARCV
#else
#define XX_ARCV_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ARCV_VERSION 0x0110U
#define ARCV_FIELDS_SIZE 34U    /* descriptor tail following the member name */
#define ARCV_CHUNK_HEADER_MIN 16U
#define ARCV_CHUNK_HEADER_MAX 4096U
#define ARCV_MAX_NAME 240U
#define ARCV_MAX_SIZE (1U << 30U) /* mirrors XArchive's MAX_LEGACY_STORE_SIZE */

typedef struct arcv_stream_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t archive_size;
    uint32_t raw_size;
    uint32_t packed_size;
    uint32_t jam_crc;
    bool wide;
    bool consumed;
} arcv_stream;

static uint16_t arcv_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t arcv_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool arcv_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Member names are fixed-width DOS strings padded with spaces or NULs. */
static char *arcv_name(const uint8_t *raw, size_t size) {
    char *result;
    size_t length = 0U;
    size_t index;
    if (!raw || size == 0U) return NULL;
    while (length < size && raw[length] != 0) ++length;
    if (length == 0U) return NULL;
    result = (char *)xx_mem_alloc(length + 1U);
    if (!result) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t value = raw[index];
        if (value < 0x20U || value == 0x7fU) {
            xx_mem_free(result);
            return NULL;
        }
        result[index] = (value == '/' || value == '\\' || value == ':' ||
                         value == '<' || value == '>' || value == '"' ||
                         value == '|' || value == '?' || value == '*')
                            ? '_'
                            : (char)value;
    }
    while (length != 0U &&
           (result[length - 1U] == ' ' || result[length - 1U] == '.'))
        --length;
    if ((length == 1U && result[0] == '.') ||
        (length == 2U && result[0] == '.' && result[1] == '.'))
        length = 0U;
    if (length == 0U) result[length++] = '_';
    result[length] = 0;
    return result;
}

static bool arcv_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    for (at = name; *at; ++at) {
        unsigned char value = (unsigned char)*at;
        if (value < 0x20U || value == ':' || value == '<' || value == '>' ||
            value == '"' || value == '|' || value == '?' || value == '*' ||
            value == '/' || value == '\\')
            return false;
    }
    return xx_rt_strcmp(name, ".") != 0 && xx_rt_strcmp(name, "..") != 0;
}

static void arcv_stream_free(void *opaque) {
    arcv_stream *stream = (arcv_stream *)opaque;
    if (!stream) return;
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

/* Reads the packed extent and, for the stock variant, decodes it.  The caller
 * owns *plain when the function returns true and plain is non-NULL. */
static bool arcv_decode(Abstractformat *format, const arcv_stream *stream,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    bool result = false;
    if (!format || !stream || stream->packed_size == 0U ||
        stream->raw_size == 0U)
        return false;
    packed = (uint8_t *)xx_mem_alloc(stream->packed_size);
    output = (uint8_t *)xx_mem_alloc(stream->raw_size);
    if (!packed || !output ||
        !arcv_read_at(format->device, stream->data_offset, packed,
                      stream->packed_size))
        goto done;
    if (!xx_arcv2_lzhuf_decode_memory(packed, stream->packed_size, output,
                                      stream->raw_size, stream->wide,
                                      &written) ||
        written != stream->raw_size)
        goto done;
    /* The stock variant checksums the plaintext; the compact one checksums
     * the packed bytes and was already verified while parsing. */
    if (stream->wide &&
        xx_crc32(XX_CRC_TYPE_CRC32_JAMCRC, output, written) != stream->jam_crc)
        goto done;
    if (plain && plain_size) {
        *plain = output;
        *plain_size = written;
        output = NULL;
    }
    result = true;
done:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return result;
}

static bool arcv_parse(Abstractformat *format, arcv_stream **result,
                       xx_pd_struct *pd) {
    uint8_t head[13U + ARCV_MAX_NAME];
    uint8_t fields[ARCV_FIELDS_SIZE];
    uint8_t chunk[ARCV_CHUNK_HEADER_MIN];
    uint8_t *packed = NULL;
    arcv_stream *stream = NULL;
    int64_t total;
    int64_t size;
    int64_t archive_header_size;
    int64_t fields_offset;
    int64_t chunk_header_size;
    int64_t data_offset;
    uint32_t name_size;
    uint32_t raw_size;
    uint32_t packed_size;
    uint32_t chunk_data_size;
    uint32_t jam_crc;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 64) return false;
    if (!arcv_read_at(format->device, format->base_address, head, 13U) ||
        xx_rt_memcmp(head, "ARCV", 4U) != 0 ||
        arcv_le16(head + 4U) != ARCV_VERSION)
        return false;
    archive_header_size = (int64_t)arcv_le16(head + 6U);
    name_size = head[12U];
    if (name_size == 0U || name_size > ARCV_MAX_NAME) return false;
    fields_offset = 13 + (int64_t)name_size;
    /* Every declared extent is bounded against the real file before use. */
    if (archive_header_size != fields_offset + (int64_t)ARCV_FIELDS_SIZE ||
        fields_offset > size || archive_header_size > size ||
        size - archive_header_size < (int64_t)ARCV_CHUNK_HEADER_MIN)
        return false;
    if (!arcv_read_at(format->device, format->base_address + 13, head,
                      name_size) ||
        !arcv_read_at(format->device, format->base_address + fields_offset,
                      fields, sizeof(fields)) ||
        !arcv_read_at(format->device,
                      format->base_address + archive_header_size, chunk,
                      sizeof(chunk)) ||
        xx_rt_memcmp(chunk, "CHNK", 4U) != 0)
        return false;
    raw_size = arcv_le32(fields);
    packed_size = arcv_le32(fields + 4U);
    jam_crc = arcv_le32(fields + 24U);
    chunk_header_size = (int64_t)arcv_le16(chunk + 6U);
    chunk_data_size = arcv_le32(chunk + 12U);
    if (raw_size == 0U || raw_size > ARCV_MAX_SIZE || packed_size == 0U ||
        packed_size > ARCV_MAX_SIZE ||
        chunk_header_size < (int64_t)ARCV_CHUNK_HEADER_MIN ||
        chunk_header_size > (int64_t)ARCV_CHUNK_HEADER_MAX ||
        chunk_data_size != packed_size)
        return false;
    data_offset = archive_header_size + chunk_header_size;
    if (data_offset > size || (int64_t)packed_size > size - data_offset)
        return false;
    stream = (arcv_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->name = arcv_name(head, name_size);
    stream->header_offset = format->base_address;
    stream->header_size = archive_header_size;
    stream->data_offset = format->base_address + data_offset;
    stream->archive_size = data_offset + (int64_t)packed_size;
    stream->raw_size = raw_size;
    stream->packed_size = packed_size;
    stream->jam_crc = jam_crc;
    if (!stream->name || (pd && xx_pd_is_stopped(pd))) goto fail;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed || !arcv_read_at(format->device, stream->data_offset, packed,
                                 packed_size))
        goto fail;
    /* Compact F=32 sub-variant: the header JAMCRC covers the packed member. */
    stream->wide =
        xx_crc32(XX_CRC_TYPE_CRC32_JAMCRC, packed, packed_size) != jam_crc;
    xx_mem_free(packed);
    packed = NULL;
    /* Stock F=60 sub-variant: only a trial decode proves the reading. */
    if (stream->wide && !arcv_decode(format, stream, NULL, NULL)) goto fail;
    *result = stream;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    arcv_stream_free(stream);
    return false;
}

static bool arcv_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *arcv_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool arcv_set_record(xx_archive_record *record,
                            const arcv_stream *stream) {
    if (!record || !stream) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = stream->header_offset;
    record->header_size = stream->header_size;
    record->data_offset = stream->data_offset;
    record->compressed_size = stream->packed_size;
    return xx_archive_record_set_original_name(record, stream->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          stream->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          stream->raw_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          stream->wide ? 2U : 1U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          stream->jam_crc) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_arcv_init(xx_arcv *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ARCV_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arcv");
    xx_format_set_extension(&archive->format, "arv");
    archive->format.check_is_valid = xx_arcv_check_is_valid;
    archive->format.handle_base_info = xx_arcv_handle_base_info;
    archive->format.get_format_size = xx_arcv_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_arcv_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_arcv_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_arcv_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_arcv_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_arcv_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_arcv_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_arcv *xx_arcv_create(xx_io_device *device, int64_t base_address) {
    xx_arcv *archive = (xx_arcv *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_arcv_init(archive, device, base_address);
    return archive;
}

void xx_arcv_destroy(xx_arcv *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_arcv_free(xx_arcv *archive) {
    if (!archive) return;
    xx_arcv_destroy(archive);
    xx_mem_free(archive);
}

bool xx_arcv_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    arcv_stream *stream;
    if (!arcv_parse(format, &stream, pd)) return false;
    arcv_stream_free(stream);
    return true;
}

bool xx_arcv_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    arcv_stream *stream;
    xx_arcv *archive;
    int64_t total;
    if (!format || !arcv_parse(format, &stream, pd)) return false;
    archive = (xx_arcv *)format;
    total = xx_io_total_size(format->device);
    archive->number_of_records = 1U;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->raw_size = stream->raw_size;
    archive->packed_size = stream->packed_size;
    archive->jam_crc = stream->jam_crc;
    archive->wide = stream->wide;
    format->number_of_archive_records = 1U;
    format->format_size = stream->archive_size;
    format->overlay_offset =
        archive->archive_end < total ? archive->archive_end : -1;
    format->overlay_size =
        archive->archive_end < total ? total - archive->archive_end : 0;
    format->is_valid = true;
    format->base_info_handled = true;
    arcv_stream_free(stream);
    return true;
}

int64_t xx_arcv_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_arcv_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_arcv_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_arcv_handle_base_info(format, pd))
               ? ((xx_arcv *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_arcv_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    arcv_stream *stream;
    xx_archive_record_state *state;
    if (!arcv_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        arcv_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = arcv_stream_free;
    state->total_records = 1U;
    if (!arcv_copy_options(&state->options, options) ||
        !arcv_set_record(&state->current_record, stream)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_arcv_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_arcv_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    arcv_stream *stream;
    (void)pd;
    /* Exactly one member per archive: the walk always ends after the first. */
    if (!format || !state || state->format != format ||
        !(stream = (arcv_stream *)state->internal_state) || stream->consumed) {
        if (state) state->has_record = false;
        return false;
    }
    stream->consumed = true;
    state->has_record = false;
    return false;
}

bool xx_arcv_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    arcv_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (arcv_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!arcv_safe_output_name(stream->name) ||
        !arcv_decode(format, stream, &plain, &plain_size))
        goto done;
    path_option = arcv_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
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
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_arcv_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
