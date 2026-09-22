/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * GX Library / Genus Graphics Library (.GXL), by Genus Microprogramming.
 *
 * A 128-byte archive header (0x01 0xCA, a 50-byte copyright banner, a u16
 * version, a 40-byte volume label, a u16 member count and 32 unused bytes) is
 * followed at offset 128 by a contiguous table of 26-byte member entries.  The
 * table is not interleaved with payloads: every entry carries the ABSOLUTE
 * file offset of its own data.  Only packing method 0 (stored) is defined, and
 * for it the original size equals the packed size.
 *
 * Layout ported from deark's shared pcxlib/gxlib module (FMT_GXLIB paths).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gxl/xx_gxl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Self-healing file-type shim: the enum entry is added by the coordinator. */
#ifdef GXL
#define XX_GXL_FILE_TYPE XX_FILE_TYPE_GXL
#else
#define XX_GXL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GXL_HEADER_SIZE 128U
#define GXL_RECORD_SIZE 26U
#define GXL_BASE_FIELD 8U
#define GXL_EXT_FIELD 4U
#define GXL_NAME_FIELD 13U /* 8 + 4 + one unused trailing byte */
#define GXL_VERSION 100U
#define GXL_METHOD_STORED 0U
#define GXL_MAX_MEMBERS 65535U

typedef struct gxl_member_s {
    char name[GXL_BASE_FIELD + GXL_EXT_FIELD + 1U];
    int64_t record_offset;
    int64_t data_offset;
    uint32_t data_size;
    uint16_t dos_date;
    uint16_t dos_time;
} gxl_member;

typedef struct gxl_stream_s {
    gxl_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint16_t format_version;
} gxl_stream;

static uint16_t gxl_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t gxl_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool gxl_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Copy a fixed-width DOS field: stop at NUL, then strip trailing spaces. */
static size_t gxl_trim_field(const uint8_t *bytes, size_t field, char *out) {
    size_t length = field;
    size_t index;
    for (index = 0U; index < field; ++index) {
        if (bytes[index] == 0U) {
            length = index;
            break;
        }
    }
    while (length > 0U && bytes[length - 1U] == ' ') --length;
    for (index = 0U; index < length; ++index) out[index] = (char)bytes[index];
    return length;
}

static bool gxl_name_char_ok(unsigned char value) {
    return value >= 0x21U && value <= 0x7eU && value != '/' && value != '\\' &&
           value != ':' && value != '<' && value != '>' && value != '"' &&
           value != '|' && value != '?' && value != '*';
}

/* +0x01 base name (8), +0x09 extension including its dot (4).  An extension
 * field that trims to one character or less means "no extension". */
static bool gxl_build_name(const uint8_t *bytes, char *out) {
    char extension[GXL_EXT_FIELD];
    size_t base_length = gxl_trim_field(bytes, GXL_BASE_FIELD, out);
    size_t ext_length = gxl_trim_field(bytes + GXL_BASE_FIELD, GXL_EXT_FIELD,
                                       extension);
    size_t index;
    if (base_length == 0U) return false;
    if (ext_length > 1U) {
        if (extension[0] != '.') return false;
        for (index = 0U; index < ext_length; ++index)
            out[base_length + index] = extension[index];
        base_length += ext_length;
    }
    out[base_length] = 0;
    for (index = 0U; index < base_length; ++index) {
        if (!gxl_name_char_ok((unsigned char)out[index])) return false;
    }
    if (out[0] == '.' && (base_length == 1U ||
                          (base_length == 2U && out[1] == '.')))
        return false;
    return true;
}

static void gxl_stream_free(void *opaque) {
    gxl_stream *stream = (gxl_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool gxl_parse(Abstractformat *format, gxl_stream **result,
                      xx_pd_struct *pd) {
    uint8_t header[GXL_HEADER_SIZE];
    gxl_stream *stream = NULL;
    int64_t total, size, table_size, end;
    uint16_t count, index;
    bool valid = false;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)(GXL_HEADER_SIZE + GXL_RECORD_SIZE) ||
        !gxl_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;

    /* Signature: 0x01 0xCA, the Genus copyright banner, version 100. */
    if (header[0] != 0x01U || header[1] != 0xcaU ||
        xx_rt_memcmp(header + 2, "Copyri", 6U) != 0 ||
        gxl_le16(header + 52) != GXL_VERSION)
        return false;

    count = gxl_le16(header + 94);
    if (count == 0U || count > GXL_MAX_MEMBERS) return false;
    table_size = (int64_t)count * (int64_t)GXL_RECORD_SIZE;
    /* The declared member count must fit in the real file before it is used
     * to allocate or loop. */
    if (table_size > size - (int64_t)GXL_HEADER_SIZE) return false;

    stream = (gxl_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->items = (gxl_member *)xx_mem_calloc((size_t)count,
                                                sizeof(*stream->items));
    if (!stream->items) goto done;
    stream->format_version = gxl_le16(header + 52);
    end = (int64_t)GXL_HEADER_SIZE + table_size;

    for (index = 0U; index < count; ++index) {
        uint8_t record[GXL_RECORD_SIZE];
        gxl_member *member = &stream->items[index];
        int64_t record_offset = (int64_t)GXL_HEADER_SIZE +
                                (int64_t)index * (int64_t)GXL_RECORD_SIZE;
        uint32_t data_offset, data_size;

        if ((pd && xx_pd_is_stopped(pd)) ||
            !gxl_read_at(format->device, format->base_address + record_offset,
                         record, sizeof(record)))
            goto done;
        /* Only "stored" is defined; anything else is unsupported, not a guess. */
        if (record[0] != GXL_METHOD_STORED) goto done;
        if (!gxl_build_name(record + 1, member->name)) goto done;

        data_offset = gxl_le32(record + 1U + GXL_NAME_FIELD);
        data_size = gxl_le32(record + 1U + GXL_NAME_FIELD + 4U);
        member->dos_time = gxl_le16(record + 1U + GXL_NAME_FIELD + 8U);
        member->dos_date = gxl_le16(record + 1U + GXL_NAME_FIELD + 10U);

        /* Both halves of the extent must lie inside the real file. */
        if ((int64_t)data_offset < (int64_t)GXL_HEADER_SIZE + table_size ||
            (int64_t)data_offset > size ||
            (int64_t)data_size > size - (int64_t)data_offset)
            goto done;

        member->record_offset = format->base_address + record_offset;
        member->data_offset = format->base_address + (int64_t)data_offset;
        member->data_size = data_size;
        if ((int64_t)data_offset + (int64_t)data_size > end)
            end = (int64_t)data_offset + (int64_t)data_size;
    }

    stream->count = (size_t)count;
    stream->archive_size = end;
    valid = true;
done:
    if (!valid) {
        gxl_stream_free(stream);
        return false;
    }
    *result = stream;
    return true;
}

static bool gxl_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *gxl_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == id) return &item->var;
    }
    return NULL;
}

static bool gxl_set_record(xx_archive_record *record, const gxl_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->record_offset;
    record->header_size = GXL_RECORD_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          GXL_METHOD_STORED) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_gxl_init(xx_gxl *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GXL_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-gxlib");
    xx_format_set_extension(&archive->format, "gxl");
    archive->format.check_is_valid = xx_gxl_check_is_valid;
    archive->format.handle_base_info = xx_gxl_handle_base_info;
    archive->format.get_format_size = xx_gxl_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gxl_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gxl_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gxl_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gxl_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gxl_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gxl_free_archive_records_reading;
}

xx_gxl *xx_gxl_create(xx_io_device *device, int64_t base_address) {
    xx_gxl *archive = (xx_gxl *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_gxl_init(archive, device, base_address);
    return archive;
}

void xx_gxl_destroy(xx_gxl *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_gxl_free(xx_gxl *archive) {
    if (!archive) return;
    xx_gxl_destroy(archive);
    xx_mem_free(archive);
}

bool xx_gxl_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    gxl_stream *stream;
    if (!gxl_parse(format, &stream, pd)) return false;
    gxl_stream_free(stream);
    return true;
}

bool xx_gxl_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    gxl_stream *stream;
    xx_gxl *archive;
    if (!format || !gxl_parse(format, &stream, pd)) return false;
    archive = (xx_gxl *)format;
    archive->number_of_records = stream->count;
    archive->format_version = stream->format_version;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    gxl_stream_free(stream);
    return true;
}

int64_t xx_gxl_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gxl_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_gxl_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gxl_handle_base_info(format, pd))
               ? ((xx_gxl *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_gxl_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    gxl_stream *stream;
    xx_archive_record_state *state;
    if (!gxl_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        gxl_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = gxl_stream_free;
    state->total_records = stream->count;
    if (!gxl_copy_options(&state->options, options) ||
        !gxl_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_gxl_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_gxl_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    gxl_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (gxl_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = gxl_set_record(&state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_gxl_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    gxl_stream *stream;
    const gxl_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (gxl_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];

    plain = (uint8_t *)xx_mem_alloc(member->data_size != 0U ? member->data_size
                                                           : 1U);
    if (!plain) goto done;
    if (member->data_size != 0U &&
        !gxl_read_at(format->device, member->data_offset, plain,
                     member->data_size))
        goto done;

    path_option = gxl_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        result = true;
        while (written < (size_t)member->data_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         (size_t)member->data_size - written);
            if (amount <= 0 ||
                (size_t)amount > (size_t)member->data_size - written) {
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

void xx_gxl_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
