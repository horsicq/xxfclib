/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/arj/xx_arj.h"

#include "xxfclib/algo/arj/xx_arj_dec.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#define XX_ARJ_MAX_HEADER 2600U
#define XX_ARJ_FLAG_GARBLED 0x01U
#define XX_ARJ_FLAG_VOLUME 0x04U
#define XX_ARJ_FLAG_EXTFILE 0x08U
#define XX_ARJ_FILE_DIRECTORY 3U

typedef struct xx_arj_member_s {
    int64_t header_offset;
    int64_t data_offset;
    uint32_t packed_size;
    uint32_t original_size;
    uint32_t crc32;
    uint32_t timestamp;
    uint8_t method;
    uint8_t flags;
    uint8_t file_type;
    char *name;
} xx_arj_member;

typedef struct xx_arj_stream_s {
    xx_arj_member *items;
    size_t count;
    size_t index;
} xx_arj_stream;

static uint16_t xx_arj_u16(const uint8_t *data) {
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t xx_arj_u32(const uint8_t *data) {
    return (uint32_t)xx_arj_u16(data) |
           ((uint32_t)xx_arj_u16(data + 2U) << 16U);
}

static bool xx_arj_read_at(xx_io_device *device, int64_t offset,
                           void *buffer, size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || offset > LONG_MAX ||
        xx_io_seek(device, (long)offset, SEEK_SET)) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static void xx_arj_stream_free(void *pointer) {
    xx_arj_stream *stream = (xx_arj_stream *)pointer;
    size_t i;
    if (!stream) return;
    for (i = 0U; i < stream->count; ++i) {
        if (stream->items[i].name) xx_str_free(stream->items[i].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_arj_read_header(xx_io_device *device, int64_t total,
                               int64_t *offset, uint8_t **body,
                               uint16_t *body_size) {
    uint8_t prefix[4];
    uint8_t crc_bytes[4];
    uint16_t size;
    int64_t cursor;
    if (!device || !offset || !body || !body_size || *offset < 0 ||
        *offset > total || total - *offset < 4 ||
        !xx_arj_read_at(device, *offset, prefix, sizeof(prefix)) ||
        prefix[0] != 0x60U || prefix[1] != 0xeaU) return false;
    *body = NULL;
    size = xx_arj_u16(prefix + 2U);
    if (size == 0U) {
        *offset += 4;
        *body_size = 0U;
        return true;
    }
    if (size < 30U || size > XX_ARJ_MAX_HEADER ||
        total - *offset < (int64_t)size + 8) return false;
    *body = (uint8_t *)xx_mem_alloc(size);
    if (!*body || !xx_arj_read_at(device, *offset + 4, *body, size) ||
        !xx_arj_read_at(device, *offset + 4 + size, crc_bytes, 4U) ||
        xx_crc32_calc(0U, *body, size) != xx_arj_u32(crc_bytes)) {
        if (*body) xx_mem_free(*body);
        *body = NULL;
        return false;
    }
    cursor = *offset + 8 + size;
    for (;;) {
        uint8_t size_bytes[2];
        uint16_t extended_size;
        if (cursor > total || total - cursor < 2 ||
            !xx_arj_read_at(device, cursor, size_bytes, 2U)) goto fail;
        extended_size = xx_arj_u16(size_bytes);
        cursor += 2;
        if (extended_size == 0U) break;
        if (cursor > total || total - cursor < (int64_t)extended_size + 4)
            goto fail;
        cursor += (int64_t)extended_size + 4;
    }
    *offset = cursor;
    *body_size = size;
    return true;
fail:
    xx_mem_free(*body);
    *body = NULL;
    return false;
}

static bool xx_arj_parse(Abstractformat *format, xx_arj_stream **result,
                         int64_t *archive_end) {
    int64_t total;
    int64_t offset;
    uint8_t *body = NULL;
    uint16_t body_size = 0U;
    xx_arj_stream *stream;
    if (!format || !format->device || !result || !archive_end ||
        format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    offset = format->base_address;
    if (total < 0 || offset > total ||
        !xx_arj_read_header(format->device, total, &offset, &body,
                            &body_size) || body_size == 0U) return false;
    xx_mem_free(body);
    body = NULL;
    stream = (xx_arj_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    for (;;) {
        int64_t header_offset = offset;
        int64_t data_offset;
        uint32_t packed_size;
        size_t name_offset;
        size_t name_size = 0U;
        xx_arj_member *grown;
        xx_arj_member *member;
        if (!xx_arj_read_header(format->device, total, &offset, &body,
                                &body_size)) goto fail;
        if (body_size == 0U) break;
        if (body[0] < 30U || body[0] >= body_size) goto fail;
        packed_size = xx_arj_u32(body + 12U);
        data_offset = offset;
        if (data_offset > total ||
            (uint64_t)packed_size > (uint64_t)(total - data_offset)) goto fail;
        name_offset = body[0];
        while (name_offset + name_size < body_size &&
               body[name_offset + name_size] != 0U) ++name_size;
        if (name_offset + name_size >= body_size) goto fail;
        grown = (xx_arj_member *)xx_mem_realloc(
            stream->items, (stream->count + 1U) * sizeof(*grown));
        if (!grown) goto fail;
        stream->items = grown;
        member = &stream->items[stream->count];
        xx_mem_zero(member, sizeof(*member));
        member->name = (char *)xx_mem_alloc(name_size + 1U);
        if (!member->name) goto fail;
        xx_mem_copy(member->name, body + name_offset, name_size);
        member->name[name_size] = '\0';
        member->header_offset = header_offset;
        member->data_offset = data_offset;
        member->packed_size = packed_size;
        member->original_size = xx_arj_u32(body + 16U);
        member->crc32 = xx_arj_u32(body + 20U);
        member->timestamp = xx_arj_u32(body + 8U);
        member->method = body[5];
        member->flags = body[4];
        member->file_type = body[6];
        ++stream->count;
        xx_mem_free(body);
        body = NULL;
        offset = data_offset + packed_size;
    }
    *result = stream;
    *archive_end = offset;
    return true;
fail:
    if (body) xx_mem_free(body);
    xx_arj_stream_free(stream);
    return false;
}

static bool xx_arj_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t i;
    if (!destination || !source) return source == NULL;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
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

static const xx_var *xx_arj_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t i;
    if (!options) return NULL;
    for (i = 0U; i < options->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_arj_safe_name(const char *name) {
    const char *component;
    const char *cursor;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    if ((((unsigned char)name[0] >= 'A' && (unsigned char)name[0] <= 'Z') ||
         ((unsigned char)name[0] >= 'a' && (unsigned char)name[0] <= 'z')) &&
        name[1] == ':') return false;
    component = name;
    for (cursor = name;; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' ||
            ch == '?' || ch == '*' || (ch != 0U && ch < 32U)) return false;
        if (ch == '/' || ch == '\\' || ch == 0U) {
            size_t length = (size_t)(cursor - component);
            if (length == 0U || (length == 1U && component[0] == '.') ||
                (length == 2U && component[0] == '.' && component[1] == '.'))
                return false;
            if (ch == 0U) return true;
            component = cursor + 1;
        }
    }
}

static bool xx_arj_set_record(xx_archive_record *record,
                              const xx_arj_member *member) {
    bool folder;
    if (!record || !member) return false;
    folder = member->file_type == XX_ARJ_FILE_DIRECTORY;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->original_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc32) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           (member->flags &
                                            XX_ARJ_FLAG_GARBLED) != 0U);
}

static bool xx_arj_write_all(xx_io_device *output, const uint8_t *data,
                             size_t size, xx_pd_struct *pd) {
    size_t offset = 0U;
    while (offset < size) {
        ssize_t amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
        amount = xx_io_write(output, data + offset, size - offset);
        if (amount <= 0 || (size_t)amount > size - offset) return false;
        offset += (size_t)amount;
    }
    return true;
}

static bool xx_arj_decode_member(Abstractformat *format,
                                 const xx_arj_member *member,
                                 uint8_t **decoded,
                                 size_t *decoded_size,
                                 xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool result = false;
    if (!format || !member || !decoded || !decoded_size ||
        (pd && xx_pd_is_stopped(pd)) || member->method > 4U ||
        (member->flags & (XX_ARJ_FLAG_GARBLED | XX_ARJ_FLAG_VOLUME |
                          XX_ARJ_FLAG_EXTFILE)) != 0U) return false;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size
                                         ? (size_t)member->packed_size
                                         : 1U);
    plain = (uint8_t *)xx_mem_alloc(member->original_size
                                        ? (size_t)member->original_size
                                        : 1U);
    if (!packed || !plain ||
        (member->packed_size != 0U &&
         !xx_arj_read_at(format->device, member->data_offset, packed,
                         member->packed_size)) ||
        !xx_arj_decode_memory(member->method, packed, member->packed_size,
                              plain, member->original_size, &written) ||
        written != member->original_size ||
        xx_crc32_calc(0U, plain, written) != member->crc32 ||
        (pd && xx_pd_is_stopped(pd))) goto cleanup;
    *decoded = plain;
    *decoded_size = written;
    plain = NULL;
    result = true;
cleanup:
    if (packed) xx_mem_free(packed);
    if (plain) xx_mem_free(plain);
    return result;
}

void xx_arj_init(xx_arj *arj, xx_io_device *device, int64_t base_address) {
    if (!arj) return;
    xx_mem_zero(arj, sizeof(*arj));
    xx_format_init(&arj->format, device, base_address);
    arj->format.file_type = XX_FILE_TYPE_ARJ;
    arj->format.format_type = XX_TYPE_ARCHIVE;
    arj->format.is_archive = true;
    xx_format_set_extension(&arj->format, "arj");
    arj->format.check_is_valid = xx_arj_check_is_valid;
    arj->format.handle_base_info = xx_arj_handle_base_info;
    arj->format.get_format_size = xx_arj_get_format_size;
    arj->format.get_number_of_archive_records =
        xx_arj_get_number_of_archive_records;
    arj->format.create_archive_records_reading =
        xx_arj_create_archive_records_reading;
    arj->format.get_current_archive_record = xx_arj_get_current_archive_record;
    arj->format.unpack_current_archive_record =
        xx_arj_unpack_current_archive_record;
    arj->format.archive_record_move_to_next =
        xx_arj_archive_record_move_to_next;
    arj->format.free_archive_records_reading =
        xx_arj_free_archive_records_reading;
    arj->archive_end = -1;
}

xx_arj *xx_arj_create(xx_io_device *device, int64_t base_address) {
    xx_arj *arj = (xx_arj *)xx_mem_alloc(sizeof(*arj));
    if (arj) xx_arj_init(arj, device, base_address);
    return arj;
}

void xx_arj_destroy(xx_arj *arj) {
    if (arj) xx_format_cleanup_extra_parameters(&arj->format);
}

void xx_arj_free(xx_arj *arj) {
    if (!arj) return;
    xx_arj_destroy(arj);
    xx_mem_free(arj);
}

bool xx_arj_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    xx_arj_stream *stream;
    int64_t end;
    (void)pd;
    if (!format || !xx_arj_parse(format, &stream, &end)) return false;
    xx_arj_stream_free(stream);
    return true;
}

bool xx_arj_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_arj_stream *stream;
    int64_t end;
    xx_arj *arj;
    (void)pd;
    if (!format || !xx_arj_parse(format, &stream, &end)) return false;
    arj = (xx_arj *)format;
    arj->number_of_records = stream->count;
    arj->archive_end = end;
    format->format_size = end - format->base_address;
    format->number_of_archive_records = stream->count;
    format->is_valid = true;
    format->base_info_handled = true;
    xx_arj_stream_free(stream);
    return true;
}

int64_t xx_arj_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_arj_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_arj_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format &&
                   (format->base_info_handled ||
                    xx_arj_handle_base_info(format, pd))
               ? ((xx_arj *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_arj_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_arj_stream *stream;
    int64_t end;
    (void)pd;
    if (!format || !xx_arj_parse(format, &stream, &end)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_arj_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = xx_arj_stream_free;
    state->total_records = stream->count;
    if (!xx_arj_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_arj_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    return state;
}

const xx_archive_record *xx_arj_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_arj_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_arj_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (xx_arj_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = xx_arj_set_record(&state->current_record,
                                          &stream->items[stream->index]);
    return state->has_record;
}

bool xx_arj_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_arj_stream *stream;
    const xx_arj_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    uint8_t *decoded = NULL;
    size_t decoded_size = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (xx_arj_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!xx_arj_safe_name(member->name)) return false;
    path_option = xx_arj_find_option(&state->options,
                                     XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        if (member->file_type == XX_ARJ_FILE_DIRECTORY) return true;
        result = xx_arj_decode_member(format, member, &decoded, &decoded_size,
                                      pd);
        if (decoded) xx_mem_free(decoded);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\')
        destination = xx_str_concat3(base, "/", member->name);
    else
        destination = xx_str_concat(base, member->name);
    if (!destination) goto cleanup;
    if (member->file_type == XX_ARJ_FILE_DIRECTORY) {
        result = xx_store_create_dirs_a(destination, true);
        goto cleanup;
    }
    if (!xx_arj_decode_member(format, member, &decoded, &decoded_size, pd) ||
        !xx_store_create_dirs_a(destination, false)) goto cleanup;
    {
        xx_io_device *output = xx_io_file_open(destination, "wb");
        if (!output) goto cleanup;
        created = true;
        result = xx_arj_write_all(output, decoded, decoded_size, pd);
        xx_io_close(output);
    }
cleanup:
    if (!result && created) xx_rt_remove(destination);
    if (decoded) xx_mem_free(decoded);
    if (destination) xx_str_free(destination);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_arj_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
