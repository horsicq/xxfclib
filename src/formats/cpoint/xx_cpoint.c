/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * cPoint installer streams contain small type-0 setup records followed by
 * type-3 stored files.  A type-0 record has {u32 tag, u8 zero, u32 size,
 * u32 zero, data[size]}; a file has {u8 3, u32 size, u32 name_length,
 * u32 metadata, NUL-terminated name, data[size], u32 footer}.  Other setup
 * records after the file run are not filesystem members.  The leading three
 * metadata tags (124, 125, 126) and the complete bounded record walk are
 * required because the container has no conventional magic.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cpoint/xx_cpoint.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define CPOINT_META_HEADER_SIZE 13U
#define CPOINT_FILE_HEADER_SIZE 13U
#define CPOINT_FOOTER_SIZE 4U
#define CPOINT_MAX_META 1024U
#define CPOINT_MAX_META_SIZE (1024U * 1024U)
#define CPOINT_MAX_FILES 65536U
#define CPOINT_MAX_NAME 256U
#define CPOINT_MAX_INPUT (UINT64_C(1024) * 1024U * 1024U)

typedef struct cpoint_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    uint32_t size;
} cpoint_member;

typedef struct cpoint_stream_s {
    cpoint_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} cpoint_stream;

static bool cpoint_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static char *cpoint_name(const uint8_t *bytes, uint32_t length) {
    char *name;
    size_t index, segment = 0U;
    if (!bytes || length < 2U || length > CPOINT_MAX_NAME ||
        bytes[length - 1U] != 0U || bytes[0] == '/' || bytes[0] == '\\')
        return NULL;
    for (index = 0U; index + 1U < length; ++index) {
        uint8_t c = bytes[index];
        if (c == 0U || c < 0x20U || c >= 0x7fU || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return NULL;
        if (c == '/' || c == '\\') {
            size_t component = index - segment;
            if (component == 0U ||
                (component == 1U && bytes[segment] == '.') ||
                (component == 2U && bytes[segment] == '.' &&
                 bytes[segment + 1U] == '.')) return NULL;
            segment = index + 1U;
        }
    }
    if (segment + 1U >= length ||
        (length - 1U - segment == 1U && bytes[segment] == '.') ||
        (length - 1U - segment == 2U && bytes[segment] == '.' &&
         bytes[segment + 1U] == '.')) return NULL;
    name = (char *)xx_mem_alloc(length);
    if (!name) return NULL;
    for (index = 0U; index + 1U < length; ++index)
        name[index] = bytes[index] == '\\' ? '/' : (char)bytes[index];
    name[length - 1U] = 0;
    return name;
}

static void cpoint_stream_free(void *opaque) {
    cpoint_stream *stream = (cpoint_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool cpoint_add(cpoint_stream *stream, const cpoint_member *member) {
    cpoint_member *grown;
    if (!stream || !member || stream->count >= CPOINT_MAX_FILES ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (cpoint_member *)xx_mem_realloc(stream->items,
                                            (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

static bool cpoint_parse(Abstractformat *format, cpoint_stream **result) {
    cpoint_stream *stream = NULL;
    int64_t total, size, cursor = 0;
    size_t meta_count = 0U;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < 3 * (int64_t)CPOINT_META_HEADER_SIZE ||
        (uint64_t)size > CPOINT_MAX_INPUT) return false;
    while (meta_count < CPOINT_MAX_META) {
        uint8_t header[CPOINT_META_HEADER_SIZE];
        uint32_t tag, payload_size;
        if (cursor > size - (int64_t)sizeof(header) ||
            !cpoint_read_at(format->device, format->base_address + cursor,
                            header, sizeof(header))) return false;
        if (header[0] == 3U) break;
        tag = xx_data_get_u32(header, 4, 0, false);
        payload_size = xx_data_get_u32(header + 5U, 4, 0, false);
        if (header[4] != 0U || xx_data_get_u32(header + 9U, 4, 0, false) != 0U ||
            payload_size > CPOINT_MAX_META_SIZE ||
            (int64_t)payload_size > size - cursor -
                                    (int64_t)sizeof(header) ||
            (meta_count < 3U && tag != 124U + (uint32_t)meta_count))
            return false;
        cursor += (int64_t)sizeof(header) + payload_size;
        ++meta_count;
    }
    if (meta_count < 3U || meta_count == CPOINT_MAX_META) return false;
    stream = (cpoint_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    while (cursor <= size - (int64_t)CPOINT_FILE_HEADER_SIZE &&
           stream->count < CPOINT_MAX_FILES) {
        uint8_t header[CPOINT_FILE_HEADER_SIZE];
        uint8_t raw_name[CPOINT_MAX_NAME];
        uint32_t data_size, name_size;
        cpoint_member member;
        int64_t data_offset;
        if (!cpoint_read_at(format->device, format->base_address + cursor,
                            header, sizeof(header))) goto fail;
        if (header[0] != 3U) break;
        data_size = xx_data_get_u32(header + 1U, 4, 0, false);
        name_size = xx_data_get_u32(header + 5U, 4, 0, false);
        if (name_size < 2U || name_size > CPOINT_MAX_NAME ||
            (int64_t)name_size > size - cursor -
                                 (int64_t)CPOINT_FILE_HEADER_SIZE)
            goto fail;
        data_offset = cursor + (int64_t)CPOINT_FILE_HEADER_SIZE + name_size;
        if ((int64_t)data_size > size - data_offset -
                                (int64_t)CPOINT_FOOTER_SIZE ||
            !cpoint_read_at(format->device,
                            format->base_address + cursor +
                                (int64_t)CPOINT_FILE_HEADER_SIZE,
                            raw_name, name_size)) goto fail;
        xx_mem_zero(&member, sizeof(member));
        member.name = cpoint_name(raw_name, name_size);
        if (!member.name) goto fail;
        member.header_offset = format->base_address + cursor;
        member.header_size = CPOINT_FILE_HEADER_SIZE + name_size;
        member.data_offset = format->base_address + data_offset;
        member.size = data_size;
        if (!cpoint_add(stream, &member)) {
            xx_mem_free(member.name);
            goto fail;
        }
        cursor = data_offset + data_size + CPOINT_FOOTER_SIZE;
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    cpoint_stream_free(stream);
    return false;
}

static bool cpoint_copy_options(xx_list_s *destination,
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

static const xx_var *cpoint_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool cpoint_set_record(xx_archive_record *record,
                              const cpoint_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_cpoint_init(xx_cpoint *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_CPOINT;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cpoint-installer");
    xx_format_set_extension(&archive->format, "ins");
    archive->format.check_is_valid = xx_cpoint_check_is_valid;
    archive->format.handle_base_info = xx_cpoint_handle_base_info;
    archive->format.get_format_size = xx_cpoint_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cpoint_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cpoint_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cpoint_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cpoint_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cpoint_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cpoint_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_cpoint *xx_cpoint_create(xx_io_device *device, int64_t base_address) {
    xx_cpoint *archive = (xx_cpoint *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_cpoint_init(archive, device, base_address);
    return archive;
}

void xx_cpoint_destroy(xx_cpoint *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_cpoint_free(xx_cpoint *archive) {
    if (!archive) return;
    xx_cpoint_destroy(archive);
    xx_mem_free(archive);
}

bool xx_cpoint_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    cpoint_stream *stream;
    (void)pd;
    if (!cpoint_parse(format, &stream)) return false;
    cpoint_stream_free(stream);
    return true;
}

bool xx_cpoint_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    cpoint_stream *stream;
    xx_cpoint *archive;
    (void)pd;
    if (!format || !cpoint_parse(format, &stream)) return false;
    archive = (xx_cpoint *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    cpoint_stream_free(stream);
    return true;
}

int64_t xx_cpoint_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_cpoint_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_cpoint_get_number_of_archive_records(Abstractformat *format,
                                                  xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_cpoint_handle_base_info(format, pd))
               ? ((xx_cpoint *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_cpoint_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    cpoint_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!cpoint_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        cpoint_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = cpoint_stream_free;
    state->total_records = stream->count;
    if (!cpoint_copy_options(&state->options, options) ||
        !cpoint_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_cpoint_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_cpoint_archive_record_move_to_next(Abstractformat *format,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    cpoint_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (cpoint_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = cpoint_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cpoint_unpack_current_archive_record(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    cpoint_stream *stream;
    cpoint_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (cpoint_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = cpoint_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return true;
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
    result = xx_store_unpack_device_to_file(format->device, member->data_offset,
                                            member->size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_cpoint_free_archive_records_reading(Abstractformat *format,
                                            xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
