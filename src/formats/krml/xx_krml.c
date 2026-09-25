/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * KRML resource archives (game data, usually named *.eng / *.rus).
 *
 * Header (6 bytes, little-endian):
 *   0x00  char[4]   "KRML" magic
 *   0x04  uint16    number of directory records (1..65535)
 *
 * Directory: <count> records of 21 bytes each, starting at 0x06.
 *   +0x00 char[13]  member name, NUL-padded; the last byte is always forced
 *                   to NUL, so 12 characters is the real ceiling
 *   +0x0d int32     absolute file offset of the member data
 *   +0x11 int32     member size in bytes
 *
 * Member data follows the directory; every member is stored (no compression)
 * and the format carries no timestamp, no checksum and no folder flag.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/krml/xx_krml.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_KRML_COPY_CHUNK (64 * 1024)

typedef struct xx_krml_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_krml_member;

typedef struct xx_krml_stream_s {
    xx_krml_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_krml_stream;

static void xx_krml_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_krml_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_krml_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_krml_path_safe(const char *name) {
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

static void xx_krml_stream_free(void *pointer) {
    xx_krml_stream *stream = (xx_krml_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_krml_add(xx_krml_stream *stream,
                          const xx_krml_member *member) {
    xx_krml_member *grown = (xx_krml_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_krml_decode(Abstractformat *self,
                             const xx_krml_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (member->compressed_size < 0 ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    buffer = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!buffer) return false;
    if (member->compressed_size != 0 &&
        ((pd && xx_pd_is_stopped(pd)) ||
         !xx_krml_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}

#define XX_KRML_MAX_MEMBERS 65535
#define XX_KRML_HEADER_SIZE 6
#define XX_KRML_RECORD_SIZE 21
#define XX_KRML_NAME_SIZE 13

static uint16_t xx_krml_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_krml_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The 32-bit offset/size fields are signed in the original reader: a value with
 * the top bit set is a malformed record, not a 2GB+ one. */
static int64_t xx_krml_i32(const uint8_t *data)
{
    uint32_t value = xx_krml_le32(data);
    if (value & 0x80000000u) {
        return (int64_t)value - (int64_t)0x100000000LL;
    }
    return (int64_t)value;
}

/* Names live in a fixed 13-byte slot, so there is no length field to sanity
 * check; the character set is the only thing that distinguishes a real
 * directory from random bytes that happen to follow a 4-byte magic. */
static bool xx_krml_name_valid(const char *name)
{
    size_t index;
    size_t length;

    length = xx_rt_strlen(name);
    if (length == 0) {
        return false;
    }
    for (index = 0; index < length; index++) {
        uint8_t character = (uint8_t)name[index];
        if ((character < 0x20) || (character > 0x7e)) {
            return false;
        }
        /* Path separators and shell wildcards never occur in a real member
         * name and would make extraction an arbitrary-write primitive. */
        if ((character == '"') || (character == '*') || (character == '<') ||
            (character == '>') || (character == '?') || (character == '|') ||
            (character == ':') || (character == '\\') || (character == '/')) {
            return false;
        }
    }
    return true;
}

static xx_krml_stream *xx_krml_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_krml_stream *stream = NULL;
    uint8_t header[XX_KRML_HEADER_SIZE];
    uint8_t record[XX_KRML_RECORD_SIZE];
    char name[XX_KRML_NAME_SIZE];
    xx_krml_member member;
    int64_t total;
    int64_t span;
    int64_t directory_size;
    int64_t data_start;
    int64_t archive_size;
    int64_t record_offset;
    int64_t data_offset;
    int64_t size;
    int64_t end;
    int64_t index;
    int64_t count;
    size_t position;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (XX_KRML_HEADER_SIZE + XX_KRML_RECORD_SIZE)) {
        return NULL;
    }
    if (!xx_krml_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, "KRML", 4) != 0) {
        return NULL;
    }
    count = (int64_t)xx_krml_le16(header + 4);
    /* An empty directory is not a degenerate-but-valid archive here: the
     * original rejects it, and accepting it would make "KRML\0\0" a match. */
    if ((count <= 0) || (count > XX_KRML_MAX_MEMBERS)) {
        return NULL;
    }

    directory_size = count * XX_KRML_RECORD_SIZE;
    if (!xx_krml_range_within(span, XX_KRML_HEADER_SIZE, directory_size)) {
        return NULL;
    }
    data_start = XX_KRML_HEADER_SIZE + directory_size;
    archive_size = data_start;

    stream = (xx_krml_stream *)xx_mem_alloc(sizeof(xx_krml_stream));
    if (!stream) {
        return NULL;
    }
    xx_mem_zero(stream, sizeof(xx_krml_stream));

    for (index = 0; index < count; index++) {
        if (pd && xx_pd_is_stopped(pd)) {
            goto fail;
        }
        record_offset = XX_KRML_HEADER_SIZE + index * XX_KRML_RECORD_SIZE;
        if (!xx_krml_read_at(self, self->base_address + record_offset, record,
                             sizeof(record))) {
            goto fail;
        }

        /* The stored name is NUL-padded, but the last slot byte is forced to
         * NUL by the writer, so a full 13-character name cannot exist. */
        for (position = 0; position < (XX_KRML_NAME_SIZE - 1); position++) {
            name[position] = (char)record[position];
        }
        name[XX_KRML_NAME_SIZE - 1] = 0;
        for (position = 0; position < XX_KRML_NAME_SIZE; position++) {
            if (name[position] == 0) {
                break;
            }
        }
        name[position] = 0;
        if (!xx_krml_name_valid(name)) {
            goto fail;
        }

        data_offset = xx_krml_i32(record + 13);
        size = xx_krml_i32(record + 17);
        if ((data_offset < 0) || (size < 0)) {
            goto fail;
        }

        /* This is the format's real signature. A 4-byte magic plus a 16-bit
         * count is far too weak on its own; requiring the FIRST record to point
         * exactly at the end of the directory ties the count, the record width
         * and the payload together, so a false positive would have to get all
         * three consistent by accident. Do not relax this to ">=". */
        if (index == 0) {
            if (data_offset != data_start) {
                goto fail;
            }
        } else if (data_offset < data_start) {
            goto fail;
        }
        if (!xx_krml_range_within(span, data_offset, size)) {
            goto fail;
        }

        end = data_offset + size;
        if (end > archive_size) {
            archive_size = end;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) {
            goto fail;
        }
        member.header_offset = record_offset;
        member.header_size = XX_KRML_RECORD_SIZE;
        member.data_offset = data_offset;
        member.compressed_size = size;
        member.uncompressed_size = size;
        member.method = 0; /* stored: the directory has no method field */
        member.timestamp = 0;
        member.is_folder = false;
        if (!xx_krml_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
    }

    if (stream->count == 0) {
        goto fail;
    }
    stream->archive_size = archive_size;
    return stream;

fail:
    xx_krml_stream_free(stream);
    return NULL;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_krml_init(xx_krml *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_KRML;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-krml");
    xx_format_set_extension(&archive->format, "eng");
    archive->format.check_is_valid = xx_krml_check_is_valid;
    archive->format.handle_base_info = xx_krml_handle_base_info;
    archive->format.get_format_size = xx_krml_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_krml_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_krml_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_krml_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_krml_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_krml_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_krml_free_archive_records_reading;
    archive->format.destroy = xx_krml_vtable_destroy;
}

xx_krml *xx_krml_create(xx_io_device *device, int64_t base_address) {
    xx_krml *archive = (xx_krml *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_krml_init(archive, device, base_address);
    return archive;
}

void xx_krml_destroy(xx_krml *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_krml_free(xx_krml *archive) {
    if (!archive) return;
    xx_krml_destroy(archive);
    xx_mem_free(archive);
}

static void xx_krml_vtable_destroy(Abstractformat *self) {
    xx_krml_destroy((xx_krml *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_krml_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_krml_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_krml_parse(self, pd);
    if (!stream) return false;
    xx_krml_stream_free(stream);
    return true;
}

bool xx_krml_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_krml *archive = (xx_krml *)self;
    xx_krml_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_krml_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_krml_stream_free(stream);
    return true;
}

int64_t xx_krml_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_krml_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_krml *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_krml_set_record(xx_archive_record *record,
                                 const xx_krml_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_UNCOMPRESSED_SIZE,
               (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           member->is_folder) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_krml_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_krml_get_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_krml_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_krml_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_krml_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_krml_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_krml_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_krml_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_krml_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_krml_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_krml_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_krml_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_krml_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_krml_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_krml_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_krml_stream *stream;
    const xx_krml_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_krml_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_krml_path_safe(member->name)) return false;

    path_option = xx_krml_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_krml_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' &&
        base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_krml_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed,
                                       plain_size - completed);
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

void xx_krml_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
