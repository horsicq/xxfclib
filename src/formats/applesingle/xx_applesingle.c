/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the AppleSingle / AppleDouble wrappers (Apple's "AppleSingle
 * / AppleDouble Formats for Foreign Files Developer's Note", also codified by
 * RFC 1740).  The layout is a fixed 26-byte big-endian header -- magic, version,
 * 16 filler bytes and a 16-bit entry count -- followed by that many 12-byte
 * descriptors of (entry id, offset, length).  Every entry is stored verbatim,
 * so each one is surfaced as a stored archive record named after the meaning of
 * its entry id.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/applesingle/xx_applesingle.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* The enum entry is added by the coordinator; keep compiling until it is. */
#ifdef APPLESINGLE
#define XX_APPLESINGLE_FILE_TYPE XX_FILE_TYPE_APPLESINGLE
#else
#define XX_APPLESINGLE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define AS_HEADER_SIZE 26U
#define AS_DESCRIPTOR_SIZE 12U
#define AS_MAGIC_SINGLE UINT32_C(0x00051600)
#define AS_MAGIC_DOUBLE UINT32_C(0x00051607)
#define AS_VERSION_1 UINT32_C(0x00010000)
#define AS_VERSION_2 UINT32_C(0x00020000)
/* 16 bits of count, so the table can never describe more than this. */
#define AS_MAX_ENTRIES 65535U

typedef struct as_entry_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t data_size;
    uint32_t entry_id;
} as_entry;

typedef struct as_stream_s {
    as_entry *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t magic;
    uint32_t version;
    uint32_t number_of_entries;
} as_stream;

static uint32_t as_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint16_t as_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static bool as_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Entry ids are a small, closed, well-known set; anything outside it keeps a
 * synthetic but still unambiguous name. */
static const char *as_entry_id_name(uint32_t entry_id) {
    switch (entry_id) {
        case 1U: return "data_fork";
        case 2U: return "resource_fork";
        case 3U: return "real_name";
        case 4U: return "comment";
        case 5U: return "bw_icon";
        case 6U: return "color_icon";
        case 7U: return "file_info";
        case 8U: return "file_dates_info";
        case 9U: return "finder_info";
        case 10U: return "macintosh_file_info";
        case 11U: return "prodos_file_info";
        case 12U: return "msdos_file_info";
        case 13U: return "short_name";
        case 14U: return "afp_file_info";
        case 15U: return "directory_id";
        default: return NULL;
    }
}

static char *as_make_name(uint32_t entry_id) {
    const char *known = as_entry_id_name(entry_id);
    char digits[16];
    size_t length = 0U;
    uint32_t value = entry_id;
    if (known) return xx_str_dup(known);
    if (value == 0U) digits[length++] = '0';
    while (value != 0U && length < sizeof(digits)) {
        digits[length++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    }
    {
        char text[24];
        size_t output = 0U;
        text[output++] = 'e';
        text[output++] = 'n';
        text[output++] = 't';
        text[output++] = 'r';
        text[output++] = 'y';
        text[output++] = '_';
        while (length != 0U) text[output++] = digits[--length];
        text[output] = 0;
        return xx_str_dup(text);
    }
}

static void as_stream_free(void *opaque) {
    as_stream *stream = (as_stream *)opaque;
    size_t index;
    if (!stream) return;
    if (stream->items) {
        for (index = 0U; index < stream->count; ++index)
            if (stream->items[index].name)
                xx_str_free(stream->items[index].name);
        xx_mem_free(stream->items);
    }
    xx_mem_free(stream);
}

static bool as_parse(Abstractformat *format, as_stream **result) {
    uint8_t header[AS_HEADER_SIZE];
    uint8_t *table = NULL;
    as_stream *stream = NULL;
    int64_t total, size, end;
    uint32_t magic, version;
    uint32_t count;
    size_t table_size;
    size_t index;

    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || format->base_address > total) return false;
    size = total - format->base_address;
    if (size < (int64_t)AS_HEADER_SIZE ||
        !as_read_at(format->device, format->base_address, header,
                    sizeof(header)))
        return false;
    magic = as_be32(header);
    version = as_be32(header + 4U);
    if (magic != AS_MAGIC_SINGLE && magic != AS_MAGIC_DOUBLE) return false;
    if (version != AS_VERSION_1 && version != AS_VERSION_2) return false;
    count = as_be16(header + 24U);
    if (count == 0U || count > AS_MAX_ENTRIES) return false;
    /* Bound the descriptor table against the real file before allocating. */
    table_size = (size_t)count * AS_DESCRIPTOR_SIZE;
    if ((int64_t)table_size > size - (int64_t)AS_HEADER_SIZE) return false;

    table = (uint8_t *)xx_mem_alloc(table_size);
    if (!table) return false;
    if (!as_read_at(format->device,
                    format->base_address + (int64_t)AS_HEADER_SIZE, table,
                    table_size)) {
        xx_mem_free(table);
        return false;
    }
    stream = (as_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) {
        xx_mem_free(table);
        return false;
    }
    stream->items = (as_entry *)xx_mem_calloc(count, sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(table);
        as_stream_free(stream);
        return false;
    }
    stream->count = count;
    stream->magic = magic;
    stream->version = version;
    stream->number_of_entries = count;
    end = (int64_t)AS_HEADER_SIZE + (int64_t)table_size;

    for (index = 0U; index < (size_t)count; ++index) {
        const uint8_t *descriptor = table + index * AS_DESCRIPTOR_SIZE;
        as_entry *entry = &stream->items[index];
        uint32_t entry_id = as_be32(descriptor);
        uint32_t entry_offset = as_be32(descriptor + 4U);
        uint32_t entry_length = as_be32(descriptor + 8U);
        /* Both fields come straight off the wire: a descriptor claiming a
         * huge length must be rejected, never trusted into an allocation. */
        if ((int64_t)entry_offset > size ||
            (int64_t)entry_length > size - (int64_t)entry_offset) {
            xx_mem_free(table);
            as_stream_free(stream);
            return false;
        }
        entry->entry_id = entry_id;
        entry->header_offset =
            format->base_address + (int64_t)AS_HEADER_SIZE +
            (int64_t)(index * AS_DESCRIPTOR_SIZE);
        entry->data_offset = format->base_address + (int64_t)entry_offset;
        entry->data_size = (int64_t)entry_length;
        entry->name = as_make_name(entry_id);
        if (!entry->name) {
            xx_mem_free(table);
            as_stream_free(stream);
            return false;
        }
        if ((int64_t)entry_offset + (int64_t)entry_length > end)
            end = (int64_t)entry_offset + (int64_t)entry_length;
    }
    xx_mem_free(table);
    stream->archive_size = end;
    *result = stream;
    return true;
}

static bool as_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *as_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool as_set_record(xx_archive_record *record, const as_entry *entry) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = (int64_t)AS_DESCRIPTOR_SIZE;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->data_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          entry->entry_id) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_applesingle_init(xx_applesingle *archive, xx_io_device *device,
                         int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_APPLESINGLE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/applefile");
    xx_format_set_extension(&archive->format, "as");
    archive->format.check_is_valid = xx_applesingle_check_is_valid;
    archive->format.handle_base_info = xx_applesingle_handle_base_info;
    archive->format.get_format_size = xx_applesingle_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_applesingle_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_applesingle_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_applesingle_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_applesingle_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_applesingle_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_applesingle_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_applesingle *xx_applesingle_create(xx_io_device *device,
                                      int64_t base_address) {
    xx_applesingle *archive =
        (xx_applesingle *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_applesingle_init(archive, device, base_address);
    return archive;
}

void xx_applesingle_destroy(xx_applesingle *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_applesingle_free(xx_applesingle *archive) {
    if (!archive) return;
    xx_applesingle_destroy(archive);
    xx_mem_free(archive);
}

bool xx_applesingle_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    as_stream *stream;
    (void)pd;
    if (!as_parse(format, &stream)) return false;
    as_stream_free(stream);
    return true;
}

bool xx_applesingle_handle_base_info(Abstractformat *format,
                                     xx_pd_struct *pd) {
    as_stream *stream;
    xx_applesingle *archive;
    (void)pd;
    if (!format || !as_parse(format, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_applesingle *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->magic = stream->magic;
    archive->version = stream->version;
    archive->number_of_entries = stream->number_of_entries;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_APPLESINGLE_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    as_stream_free(stream);
    return true;
}

int64_t xx_applesingle_get_format_size(Abstractformat *format,
                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_applesingle_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_applesingle_get_number_of_archive_records(Abstractformat *format,
                                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_applesingle_handle_base_info(format, pd))
               ? ((xx_applesingle *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_applesingle_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    as_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!as_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        as_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = as_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!as_copy_options(&state->options, options) ||
        !as_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_applesingle_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_applesingle_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    as_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (as_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        as_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_applesingle_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    as_stream *stream;
    const as_entry *entry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (as_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    entry = &stream->items[stream->index];
    path_option = as_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        int64_t total = xx_io_total_size(format->device);
        return entry->data_offset >= 0 && entry->data_size >= 0 &&
               entry->data_offset <= total &&
               entry->data_size <= total - entry->data_offset;
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
               ? xx_str_concat3(base, "/", entry->name)
               : xx_str_concat(base, entry->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    result = xx_store_unpack_device_to_file(format->device, entry->data_offset,
                                            entry->data_size, path, pd);
done:
    if (!result && path) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_applesingle_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
