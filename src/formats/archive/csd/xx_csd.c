/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * IBM OS/2 service-pack ("CSD" / FixPak) compressed members.  Every file of a
 * FixPak's disk images is stored compressed on its own, one payload per file:
 *
 *   0x00  6  magic, one of six 'S' x 'S' + word variants, each selecting the
 *            key of the stream scrambler
 *   0x06  n  the scrambled bit stream: adaptive Huffman symbols over literal
 *            ranks, LZ copies and learned phrases, ending in an end code
 *
 * There is no name, size or checksum field.  The member takes the container's
 * own file name (the FixPak tools restore the file under the same name); a
 * container without a path gets a placeholder.  The gate is that a trial
 * decode reaches the end code inside the file.  The codec lives in
 * algo/csdpack.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/csd/xx_csd.h"

#include "xxfclib/algo/csdpack/xx_csdpack.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_CSD_FILE_TYPE XX_FILE_TYPE_CSD

#define XX_CSD_HEADER_SIZE ((int64_t)XX_CSDPACK_HEADER_SIZE)
/* The end code needs a few bits; the smallest real member is 30 bytes. */
#define XX_CSD_MIN_FILE_SIZE 8
#define XX_CSD_MAX_PACKED ((int64_t)64 * 1024 * 1024)
#define XX_CSD_MAX_DECODED ((size_t)256 * 1024 * 1024)
/* The densest token is a 255-byte phrase behind a one-bit symbol and a few
 * rank bits, a little under 300x; real members stay below 100x. */
#define XX_CSD_MAX_RATIO 2048U
#define XX_CSD_RATIO_SLACK ((size_t)1024 * 1024)
#define XX_CSD_METHOD_CSD 1U
#define XX_CSD_PLACEHOLDER_NAME "csd_data"
#define XX_CSD_MAX_NAME 255U

typedef struct xx_csd_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
} xx_csd_member;

typedef struct xx_csd_stream_s {
    xx_csd_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_csd_stream;

static void xx_csd_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_csd_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
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

static bool xx_csd_path_safe(const char *name)
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

/* The member name: the last component of the container's path when the
 * member starts at offset zero of a named file, else the placeholder. */
static char *xx_csd_member_name(Abstractformat *self)
{
    const char *path = self->base_address == 0 ? xx_io_source_path(self->device) : NULL;
    const char *leaf = path;
    const char *cursor;
    size_t length;

    if (path) {
        for (cursor = path; *cursor; ++cursor) {
            if (*cursor == '/' || *cursor == '\\' || *cursor == ':') leaf = cursor + 1;
        }
        length = xx_str_len(leaf);
        if (length != 0U && length <= XX_CSD_MAX_NAME && !(length <= 2U && leaf[0] == '.' && (length == 1U || leaf[1] == '.'))) {
            return xx_str_dup(leaf);
        }
    }
    return xx_str_dup(XX_CSD_PLACEHOLDER_NAME);
}

static void xx_csd_stream_free(void *pointer)
{
    xx_csd_stream *stream = (xx_csd_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static size_t xx_csd_output_limit(size_t packed)
{
    size_t limit = XX_CSD_MAX_DECODED;

    if (packed < (XX_CSD_MAX_DECODED - XX_CSD_RATIO_SLACK) / XX_CSD_MAX_RATIO) limit = packed * XX_CSD_MAX_RATIO + XX_CSD_RATIO_SLACK;
    return limit;
}

/* --------------------------------------------------------------- parse -- */

static xx_csd_stream *xx_csd_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_csd_stream *stream;
    xx_csd_member *member;
    uint8_t header[XX_CSDPACK_HEADER_SIZE];
    uint8_t *payload;
    int64_t total;
    int64_t span;
    size_t consumed = 0U;
    size_t produced = 0U;
    bool measured;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CSD_MIN_FILE_SIZE) return NULL;
    if (span > XX_CSD_MAX_PACKED) span = XX_CSD_MAX_PACKED;
    if (!xx_csd_read_at(self, self->base_address, header, sizeof(header))) return NULL;
    if (!xx_csdpack_header_key(header, sizeof(header))) return NULL;

    payload = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!payload) return NULL;
    if (!xx_csd_read_at(self, self->base_address, payload, (size_t)span)) {
        xx_mem_free(payload);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(payload);
        return NULL;
    }
    /* Measure only: the plaintext is not kept until extraction. */
    measured = xx_csdpack_decode_memory(payload, (size_t)span, NULL, xx_csd_output_limit((size_t)span), &consumed, &produced);
    xx_mem_free(payload);
    /* An empty plaintext would make extraction write an empty file and call
     * it success; such a stream is a reject, not an empty member. */
    if (!measured || produced == 0U) return NULL;
    if (consumed <= (size_t)XX_CSD_HEADER_SIZE || (int64_t)consumed > span) return NULL;

    stream = (xx_csd_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items = (xx_csd_member *)xx_mem_alloc(sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        return NULL;
    }
    member = &stream->items[0];
    xx_mem_zero(member, sizeof(*member));
    member->name = xx_csd_member_name(self);
    if (!member->name || !xx_csd_path_safe(member->name)) {
        xx_str_free(member->name);
        xx_mem_free(stream->items);
        xx_mem_free(stream);
        return NULL;
    }
    member->header_offset = self->base_address;
    member->header_size = XX_CSD_HEADER_SIZE;
    member->data_offset = self->base_address + XX_CSD_HEADER_SIZE;
    member->compressed_size = (int64_t)consumed - XX_CSD_HEADER_SIZE;
    member->uncompressed_size = (int64_t)produced;
    member->method = XX_CSD_METHOD_CSD;
    stream->count = 1U;
    stream->index = 0U;
    stream->archive_size = (int64_t)consumed;
    return stream;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_csd_decode(Abstractformat *self, const xx_csd_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *input;
    uint8_t *output;
    size_t packed;
    size_t consumed = 0U;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_CSD_METHOD_CSD) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) return false;
    if (member->compressed_size > XX_CSD_MAX_PACKED || (uint64_t)member->uncompressed_size > (uint64_t)XX_CSD_MAX_DECODED) return false;

    packed = (size_t)(member->compressed_size + XX_CSD_HEADER_SIZE);
    input = (uint8_t *)xx_mem_alloc(packed);
    if (!input) return false;
    if (!xx_csd_read_at(self, member->header_offset, input, packed)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* Exactly the measured plaintext, or nothing. */
    if (!xx_csdpack_decode_memory(input, packed, output, (size_t)member->uncompressed_size, &consumed, &written) ||
        written != (size_t)member->uncompressed_size || consumed != packed) {
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

void xx_csd_init(xx_csd *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CSD_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-csd");
    xx_format_set_extension(&archive->format, "csd");
    archive->format.check_is_valid = xx_csd_check_is_valid;
    archive->format.handle_base_info = xx_csd_handle_base_info;
    archive->format.get_format_size = xx_csd_get_format_size;
    archive->format.get_number_of_archive_records = xx_csd_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_csd_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_csd_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_csd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_csd_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_csd_free_archive_records_reading;
    archive->format.destroy = xx_csd_vtable_destroy;
}

xx_csd *xx_csd_create(xx_io_device *device, int64_t base_address)
{
    xx_csd *archive = (xx_csd *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_csd_init(archive, device, base_address);
    return archive;
}

void xx_csd_destroy(xx_csd *archive)
{
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_csd_free(xx_csd *archive)
{
    if (!archive) return;
    xx_csd_destroy(archive);
    xx_mem_free(archive);
}

static void xx_csd_vtable_destroy(Abstractformat *self)
{
    xx_csd_destroy((xx_csd *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_csd_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_csd_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_csd_parse(self, pd);
    if (!stream) return false;
    xx_csd_stream_free(stream);
    return true;
}

bool xx_csd_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_csd *archive = (xx_csd *)self;
    xx_csd_stream *stream;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_csd_parse(self, pd);
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
    xx_csd_stream_free(stream);
    return true;
}

int64_t xx_csd_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_csd_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return self->is_valid ? ((xx_csd *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_csd_set_record(xx_archive_record *record, const xx_csd_member *member)
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
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static bool xx_csd_copy_options(xx_list_s *target, const xx_list_s *options)
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

static const xx_var *xx_csd_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_csd_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_csd_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_csd_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_csd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_csd_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_csd_copy_options(&state->options, options) || (stream->count != 0U && !xx_csd_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_csd_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_csd_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_csd_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_csd_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_csd_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_csd_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_csd_stream *stream;
    const xx_csd_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_csd_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_csd_path_safe(member->name)) return false;

    path_option = xx_csd_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member. */
        result = xx_csd_decode(self, member, &plain, &plain_size, pd);
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
    if (!xx_store_create_dirs_a(target_path, false) || !xx_csd_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_csd_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
