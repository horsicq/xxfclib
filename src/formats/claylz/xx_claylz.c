/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Clay archives (*.cmz). The codec is xx_claylz_decode_memory().
 *
 *   member header, 20 bytes:
 *     0x00  u32 LE   'Clay' (0x79616c43)
 *     0x04  u32 LE   compressed size, the Clay LZ stream's own length
 *     0x08  u32 LE   uncompressed size
 *     0x0c  u16 LE   DOS date
 *     0x0e  u16 LE   DOS time
 *     0x10  u16 LE   name field length, 1 .. 4096
 *     0x12  u16      unused
 *   then name field length bytes of name, then the stream.
 *
 * There is no central directory, no member count and no terminator: the chain
 * simply ends where the next 'Clay' magic fails to appear, and everything
 * after that point is overlay. The name is a FIXED-LENGTH field rather than a
 * C string -- trailing NUL padding is normal and is dropped, and only control
 * bytes make a field implausible.
 *
 * A member whose stream is cut off by the end of the file still lists, with
 * its compressed size clamped to what is actually there; the decoder reports
 * the truncation on its own when the member is extracted.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/claylz/xx_claylz.h"

#include "xxfclib/algo/claylz/xx_claylz.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* The alias macro is defined next to the enumerator in xxfc_defs.h, so testing
 * for it picks up the real file type as soon as CLAYLZ is registered there.
 * Until then the reader identifies itself as unknown rather than borrowing
 * another format's id. See the port report for the registration this needs. */
#ifdef CLAYLZ
#define XX_CLAYLZ_FILE_TYPE XX_FILE_TYPE_CLAYLZ
#else
#define XX_CLAYLZ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_CLAYLZ_HEADER_SIZE 20
#define XX_CLAYLZ_MAGIC 0x79616c43U /* 'C' 'l' 'a' 'y' little-endian */
#define XX_CLAYLZ_MAX_MEMBERS 100000
#define XX_CLAYLZ_MAX_NAME_SIZE 4096
/* Both sizes are attacker-controlled u32 fields; the reference reads them into
 * a signed 32-bit value and stops the chain on a negative, so anything at or
 * above 0x80000000 ends the walk here too. */
#define XX_CLAYLZ_MAX_SIZE ((int64_t)0x7fffffff)

typedef struct xx_claylz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint64_t timestamp; /* DOS date in the high half, DOS time in the low */
} xx_claylz_member;

typedef struct xx_claylz_stream_s {
    xx_claylz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_claylz_stream;

static void xx_claylz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t xx_claylz_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_claylz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool xx_claylz_read_at(Abstractformat *self, int64_t offset,
                              uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static bool xx_claylz_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. Clay names come
 * from DOS and may carry either separator, so both are treated as one. */
static bool xx_claylz_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    /* A drive letter would make the path absolute on the host. */
    if (name[0] != '\0' && name[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/' && *end != '\\') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

/* The name field is fixed length, not NUL-terminated. Control bytes make it
 * implausible; NUL is padding and is dropped wherever it appears, which is
 * what the reference's remove(QChar('\0')) does. */
static char *xx_claylz_name_from_field(const uint8_t *field, size_t size) {
    char *name;
    size_t length = 0U;
    size_t index;

    if (!field || size == 0U) return NULL;
    for (index = 0U; index < size; ++index) {
        if (field[index] < 0x20U && field[index] != 0U) return NULL;
        if (field[index] != 0U) ++length;
    }
    if (length == 0U) return NULL;
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    length = 0U;
    for (index = 0U; index < size; ++index) {
        if (field[index] != 0U) name[length++] = (char)field[index];
    }
    name[length] = '\0';
    return name;
}

static void xx_claylz_stream_free(void *pointer) {
    xx_claylz_stream *stream = (xx_claylz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of member->name. */
static bool xx_claylz_add(xx_claylz_stream *stream,
                          const xx_claylz_member *member) {
    xx_claylz_member *grown = (xx_claylz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* --------------------------------------------------------------- parse -- */

static xx_claylz_stream *xx_claylz_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_claylz_stream *stream;
    xx_claylz_member member;
    uint8_t header[XX_CLAYLZ_HEADER_SIZE];
    uint8_t *field = NULL;
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    int64_t compressed;
    int64_t uncompressed;
    int64_t name_size;
    int64_t data_offset;
    uint32_t raw;
    char *name = NULL;
    bool truncated = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)XX_CLAYLZ_HEADER_SIZE) return NULL;

    stream = (xx_claylz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (stream->count < (size_t)XX_CLAYLZ_MAX_MEMBERS) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_claylz_range_within(span, offset,
                                    (int64_t)XX_CLAYLZ_HEADER_SIZE)) {
            break;
        }
        if (!xx_claylz_read_at(self, self->base_address + offset, header,
                               sizeof(header))) {
            goto fail;
        }
        /* The magic is the whole of the chain's framing: no length field
         * points here, so a member that does not open with it is simply not
         * part of the archive and everything from here on is overlay. */
        if (xx_claylz_le32(header) != XX_CLAYLZ_MAGIC) break;

        raw = xx_claylz_le32(header + 4);
        if (raw > (uint32_t)XX_CLAYLZ_MAX_SIZE) break;
        compressed = (int64_t)raw;
        raw = xx_claylz_le32(header + 8);
        if (raw > (uint32_t)XX_CLAYLZ_MAX_SIZE) break;
        uncompressed = (int64_t)raw;

        name_size = (int64_t)xx_claylz_le16(header + 0x10);
        if (name_size <= 0 || name_size > (int64_t)XX_CLAYLZ_MAX_NAME_SIZE) {
            break;
        }
        if (!xx_claylz_range_within(span, offset + XX_CLAYLZ_HEADER_SIZE,
                                    name_size)) {
            break;
        }

        field = (uint8_t *)xx_mem_alloc((size_t)name_size);
        if (!field) goto fail;
        if (!xx_claylz_read_at(self,
                               self->base_address + offset +
                                   XX_CLAYLZ_HEADER_SIZE,
                               field, (size_t)name_size)) {
            goto fail;
        }
        name = xx_claylz_name_from_field(field, (size_t)name_size);
        xx_mem_free(field);
        field = NULL;
        if (!name) break; /* an implausible name ends the chain, as above */

        data_offset = offset + XX_CLAYLZ_HEADER_SIZE + name_size;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = (int64_t)XX_CLAYLZ_HEADER_SIZE + name_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed;
        member.uncompressed_size = uncompressed;
        member.timestamp = ((uint64_t)xx_claylz_le16(header + 0x0c) << 16) |
                           (uint64_t)xx_claylz_le16(header + 0x0e);

        /* A member whose stream runs past the end of the file still lists; it
         * just cannot be decoded past that end, which the codec reports on its
         * own. The walk stops here because there is nothing left to walk. */
        if (!xx_claylz_range_within(span, data_offset, compressed)) {
            member.compressed_size = span - data_offset;
            truncated = true;
        }
        if (!xx_claylz_add(stream, &member)) goto fail;
        name = NULL;

        if (truncated) {
            offset = span;
            break;
        }
        offset = data_offset + compressed;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = offset;
    return stream;

fail:
    if (field) xx_mem_free(field);
    if (name) xx_str_free(name);
    xx_claylz_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_claylz_decode(Abstractformat *self,
                             const xx_claylz_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size <= 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_CLAYLZ_MAX_SIZE ||
        member->uncompressed_size > XX_CLAYLZ_MAX_SIZE) {
        return false;
    }
    if (member->uncompressed_size == 0) {
        /* Nothing to decode: the member is an empty file. The codec produces
         * exactly output_size bytes and has no zero-length case. */
        *out = NULL;
        *out_size = 0U;
        return true;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_claylz_read_at(self, member->data_offset, input,
                           (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* The container stores the plaintext length, so the codec is told exactly
     * how much to produce and a short decode is a failure, not a partial. */
    if (!xx_claylz_decode_memory(input, (size_t)member->compressed_size, output,
                                 (size_t)member->uncompressed_size, &written) ||
        written != (size_t)member->uncompressed_size) {
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

void xx_claylz_init(xx_claylz *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CLAYLZ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-clay");
    xx_format_set_extension(&archive->format, "cmz");
    archive->format.check_is_valid = xx_claylz_check_is_valid;
    archive->format.handle_base_info = xx_claylz_handle_base_info;
    archive->format.get_format_size = xx_claylz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_claylz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_claylz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_claylz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_claylz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_claylz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_claylz_free_archive_records_reading;
    archive->format.destroy = xx_claylz_vtable_destroy;
}

xx_claylz *xx_claylz_create(xx_io_device *device, int64_t base_address) {
    xx_claylz *archive = (xx_claylz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_claylz_init(archive, device, base_address);
    return archive;
}

void xx_claylz_destroy(xx_claylz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_claylz_free(xx_claylz *archive) {
    if (!archive) return;
    xx_claylz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_claylz_vtable_destroy(Abstractformat *self) {
    xx_claylz_destroy((xx_claylz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_claylz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_claylz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_claylz_parse(self, pd);
    if (!stream) return false;
    xx_claylz_stream_free(stream);
    return true;
}

bool xx_claylz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_claylz *archive = (xx_claylz *)self;
    xx_claylz_stream *stream;
    int64_t total;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_claylz_parse(self, pd);
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
    xx_claylz_stream_free(stream);
    return true;
}

int64_t xx_claylz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_claylz_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_claylz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_claylz_set_record(xx_archive_record *record,
                                 const xx_claylz_member *member) {
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
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_claylz_copy_options(xx_list_s *target,
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

static const xx_var *xx_claylz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_claylz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_claylz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_claylz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_claylz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_claylz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_claylz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_claylz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_claylz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_claylz_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_claylz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_claylz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_claylz_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_claylz_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_claylz_stream *stream;
    const xx_claylz_member *member;
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
    stream = (xx_claylz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_claylz_path_safe(member->name)) return false;

    path_option =
        xx_claylz_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        result = xx_claylz_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_claylz_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_claylz_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
