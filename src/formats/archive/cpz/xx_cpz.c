/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Central Point CPZ archives (CPShrink, Central Point Backup / PC Tools 6-9:
 * CPBACK*.CPZ with DATABASE.SET, FILECHK.EXE, SCHEDULE.OV1 ...).
 *
 *   header, 8 bytes at offset 0, little endian:
 *     0x00  4  u32  member count
 *     0x04  4  u32  CRC-32 of the directory records that follow
 *   directory, count records of 32 bytes:
 *     0x00  4  u32  CRC-32 of the member's packed bytes
 *     0x04 13  char name, 8.3, NUL padded
 *     0x11  2  u16  attributes
 *     0x13  1  u8   method: 0 DCL implode (binary), 1 DCL implode (ASCII),
 *                   2 stored
 *     0x14  4  u32  unpacked size
 *     0x18  4  u32  packed size
 *     0x1C  2  u16  MS-DOS time
 *     0x1E  2  u16  MS-DOS date
 *   payloads back to back from offset 8 + 32 * count, in record order.
 *
 * Both CRC-32s are verified while parsing: the directory CRC and every
 * member's packed CRC are what make a file with no magic safely identifiable.
 * The imploded streams are PKWARE DCL ("blast") streams with their own
 * two-byte header, decoded by the library's DCL decoder.
 *
 * Recovered from the samples and verified over 181 of 181 files.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/formats/archive/cpz/xx_cpz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_CPZ_FILE_TYPE XX_FILE_TYPE_CPZ

#define XX_CPZ_HEADER_SIZE 8
#define XX_CPZ_RECORD_SIZE 32
#define XX_CPZ_NAME_FIELD 13U
#define XX_CPZ_MAX_MEMBERS 65536U
#define XX_CPZ_METHOD_IMPLODE_BINARY 0U
#define XX_CPZ_METHOD_IMPLODE_ASCII 1U
#define XX_CPZ_METHOD_STORED 2U
#define XX_CPZ_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* A DCL match codes up to 518 bytes in a few bits. */
#define XX_CPZ_MAX_RATIO 1024
#define XX_CPZ_RATIO_SLACK 64
#define XX_CPZ_PLACEHOLDER_NAME "cpz_member"

typedef struct xx_cpz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint32_t attributes;
    uint32_t dos_stamp;
    uint32_t crc;
} xx_cpz_member;

typedef struct xx_cpz_stream_s {
    xx_cpz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_cpz_stream;

static void xx_cpz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_cpz_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 || xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received = xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_cpz_path_safe(const char *name)
{
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_cpz_stream_free(void *pointer)
{
    xx_cpz_stream *stream = (xx_cpz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* The stored name with anything that is not a plain file name character
 * replaced; an empty field gets a numbered placeholder. */
static char *xx_cpz_member_name(const uint8_t *field, size_t ordinal)
{
    char buffer[XX_CPZ_NAME_FIELD + 1U];
    size_t length = 0U;
    size_t index;

    while (length < XX_CPZ_NAME_FIELD && field[length] != 0U) ++length;
    for (index = 0U; index < length; ++index) {
        uint8_t c = field[index];
        buffer[index] = (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') ? '_' : (char)c;
    }
    buffer[length] = '\0';
    while (length > 0U && (buffer[length - 1U] == ' ' || buffer[length - 1U] == '.')) buffer[--length] = '\0';
    if (length == 0U || (length <= 2U && buffer[0] == '.' && (length == 1U || buffer[1] == '.'))) {
        char fallback[32];
        snprintf(fallback, sizeof(fallback), "%s_%04u", XX_CPZ_PLACEHOLDER_NAME, (unsigned)ordinal);
        return xx_str_dup(fallback);
    }
    return xx_str_dup(buffer);
}

/* CRC-32 of @p size bytes at @p offset, read in pieces. */
static bool xx_cpz_crc_range(Abstractformat *self, int64_t offset, int64_t size, uint32_t *crc, xx_pd_struct *pd)
{
    uint8_t buffer[65536];
    int64_t done = 0;

    *crc = 0U;
    while (done < size) {
        size_t take = (size_t)((size - done) < (int64_t)sizeof(buffer) ? (size - done) : (int64_t)sizeof(buffer));
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_cpz_read_at(self, offset + done, buffer, take)) return false;
        *crc = xx_crc32_calc(*crc, buffer, take);
        done += (int64_t)take;
    }
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_cpz_stream *xx_cpz_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cpz_stream *stream = NULL;
    uint8_t header[XX_CPZ_HEADER_SIZE];
    uint8_t *directory = NULL;
    int64_t total;
    int64_t span;
    int64_t directory_size;
    int64_t offset;
    uint32_t count;
    uint32_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CPZ_HEADER_SIZE + XX_CPZ_RECORD_SIZE) return NULL;
    if (!xx_cpz_read_at(self, self->base_address, header, sizeof(header))) return NULL;
    count = xx_data_get_u32(header, 4, 0, false);
    if (count == 0U || count > XX_CPZ_MAX_MEMBERS) return NULL;
    directory_size = (int64_t)count * XX_CPZ_RECORD_SIZE;
    if (directory_size > span - XX_CPZ_HEADER_SIZE) return NULL;
    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_cpz_read_at(self, self->base_address + XX_CPZ_HEADER_SIZE, directory, (size_t)directory_size) ||
        xx_crc32_calc(0U, directory, (size_t)directory_size) != xx_data_get_u32(header + 4, 4, 0, false)) {
        xx_mem_free(directory);
        return NULL;
    }

    stream = (xx_cpz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_cpz_member *)xx_mem_alloc(sizeof(*stream->items) * count);
    if (!stream->items) {
        xx_mem_free(directory);
        xx_mem_free(stream);
        return NULL;
    }
    xx_mem_zero(stream->items, sizeof(*stream->items) * count);

    offset = XX_CPZ_HEADER_SIZE + directory_size;
    for (index = 0U; index < count; ++index) {
        const uint8_t *record = directory + (size_t)index * XX_CPZ_RECORD_SIZE;
        xx_cpz_member *member = &stream->items[index];
        uint32_t crc;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        member->method = record[0x13];
        if (member->method != XX_CPZ_METHOD_IMPLODE_BINARY && member->method != XX_CPZ_METHOD_IMPLODE_ASCII && member->method != XX_CPZ_METHOD_STORED) goto fail;
        member->uncompressed_size = (int64_t)xx_data_get_u32(record + 0x14, 4, 0, false);
        member->compressed_size = (int64_t)xx_data_get_u32(record + 0x18, 4, 0, false);
        if (member->uncompressed_size > XX_CPZ_MAX_DECODED || member->compressed_size > span - offset) goto fail;
        if (member->method == XX_CPZ_METHOD_STORED) {
            if (member->compressed_size != member->uncompressed_size) goto fail;
        } else if (member->uncompressed_size > member->compressed_size * XX_CPZ_MAX_RATIO + XX_CPZ_RATIO_SLACK) {
            goto fail;
        }
        member->crc = xx_data_get_u32(record, 4, 0, false);
        member->attributes = xx_data_get_u16(record + 0x11, 2, 0, false);
        member->dos_stamp = ((uint32_t)xx_data_get_u16(record + 0x1E, 2, 0, false) << 16) | (uint32_t)xx_data_get_u16(record + 0x1C, 2, 0, false);
        member->header_offset = self->base_address + XX_CPZ_HEADER_SIZE + (int64_t)index * XX_CPZ_RECORD_SIZE;
        member->header_size = XX_CPZ_RECORD_SIZE;
        member->data_offset = self->base_address + offset;
        member->name = xx_cpz_member_name(record + 4, index);
        stream->count = (size_t)index + 1U;
        if (!member->name || !xx_cpz_path_safe(member->name)) goto fail;
        /* The packed CRC is what authenticates the member before anything
         * is decoded. */
        if (!xx_cpz_crc_range(self, member->data_offset, member->compressed_size, &crc, pd) || crc != member->crc) goto fail;
        offset += member->compressed_size;
    }
    xx_mem_free(directory);
    stream->index = 0U;
    stream->archive_size = offset;
    return stream;

fail:
    xx_mem_free(directory);
    xx_cpz_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_cpz_decode(Abstractformat *self, const xx_cpz_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) return false;
    if (member->compressed_size > XX_CPZ_MAX_DECODED || member->uncompressed_size > XX_CPZ_MAX_DECODED) return false;

    input = (uint8_t *)xx_mem_alloc(member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!input) return false;
    if (member->compressed_size != 0 && !xx_cpz_read_at(self, member->data_offset, input, (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (member->method == XX_CPZ_METHOD_STORED) {
        *out = input;
        *out_size = (size_t)member->compressed_size;
        return true;
    }
    output = (uint8_t *)xx_mem_alloc(member->uncompressed_size != 0 ? (size_t)member->uncompressed_size : 1U);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    if (member->uncompressed_size != 0 &&
        (!xx_dcl_decode_memory(input, (size_t)member->compressed_size, output, (size_t)member->uncompressed_size, &written) || written != (size_t)member->uncompressed_size)) {
        xx_mem_free(output);
        xx_mem_free(input);
        return false;
    }
    xx_mem_free(input);
    *out = output;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_cpz_init(xx_cpz *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CPZ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cpz");
    xx_format_set_extension(&archive->format, "cpz");
    archive->format.check_is_valid = xx_cpz_check_is_valid;
    archive->format.handle_base_info = xx_cpz_handle_base_info;
    archive->format.get_format_size = xx_cpz_get_format_size;
    archive->format.get_number_of_archive_records = xx_cpz_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_cpz_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_cpz_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_cpz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_cpz_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_cpz_free_archive_records_reading;
    archive->format.destroy = xx_cpz_vtable_destroy;
}

xx_cpz *xx_cpz_create(xx_io_device *device, int64_t base_address)
{
    xx_cpz *archive = (xx_cpz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_cpz_init(archive, device, base_address);
    return archive;
}

void xx_cpz_destroy(xx_cpz *archive)
{
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_cpz_free(xx_cpz *archive)
{
    if (!archive) return;
    xx_cpz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_cpz_vtable_destroy(Abstractformat *self)
{
    xx_cpz_destroy((xx_cpz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_cpz_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cpz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_cpz_parse(self, pd);
    if (!stream) return false;
    xx_cpz_stream_free(stream);
    return true;
}

bool xx_cpz_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_cpz *archive = (xx_cpz *)self;
    xx_cpz_stream *stream;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_cpz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    total = xx_io_total_size(self->device);
    if (total > self->base_address + stream->archive_size) {
        self->overlay_offset = self->base_address + stream->archive_size;
        self->overlay_size = total - self->overlay_offset;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    xx_cpz_stream_free(stream);
    return true;
}

int64_t xx_cpz_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_cpz_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return self->is_valid ? ((xx_cpz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_cpz_set_record(xx_archive_record *record, const xx_cpz_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->dos_stamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static bool xx_cpz_copy_options(xx_list_s *target, const xx_list_s *options)
{
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) || !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_cpz_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_cpz_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_cpz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_cpz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_cpz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_cpz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_cpz_copy_options(&state->options, options) || (stream->count != 0U && !xx_cpz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_cpz_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_cpz_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_cpz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_cpz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_cpz_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cpz_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_cpz_stream *stream;
    const xx_cpz_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_cpz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_cpz_path_safe(member->name)) return false;

    path_option = xx_cpz_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = xx_cpz_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' && base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;
    if (!xx_store_create_dirs_a(target_path, false) || !xx_cpz_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        size_t completed = 0U;

        created = output != NULL;
        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_cpz_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
