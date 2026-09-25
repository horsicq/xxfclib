/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JAM resource archives.
 *
 *   0..2    "JAM"
 *   3..     the root directory node
 *
 * A node is:
 *
 *   i32 LE file count
 *   file count records of 23 bytes:
 *     0x00  name, 15 bytes, NUL terminated and NUL padded
 *     0x0F  i32 LE data offset, absolute
 *     0x13  i32 LE data size
 *   i32 LE subdirectory count
 *   subdirectory count records of 19 bytes:
 *     0x00  name, 15 bytes, NUL terminated and NUL padded
 *     0x0F  i32 LE child node offset, absolute
 *
 * The tree is flattened into one member list, subdirectory names becoming a
 * '/' separated prefix. Child offsets are absolute rather than relative to
 * their parent, so a malformed archive can point a subdirectory back at an
 * ancestor; the walk keeps the set of visited node offsets to stop that.
 *
 * The magic is only three bytes, which on its own says almost nothing. What
 * makes it safe to gate on is the first member record: its data offset must
 * be a nonzero multiple of 256, because a real writer lays the data area out
 * on 256-byte boundaries after the directory.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/jam/xx_jam.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_JAM_COPY_CHUNK (64 * 1024)

typedef struct xx_jam_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_jam_member;

typedef struct xx_jam_stream_s {
    xx_jam_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_jam_stream;

static void xx_jam_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_jam_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_jam_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_jam_path_safe(const char *name) {
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

static void xx_jam_stream_free(void *pointer) {
    xx_jam_stream *stream = (xx_jam_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_jam_add(xx_jam_stream *stream,
                          const xx_jam_member *member) {
    xx_jam_member *grown = (xx_jam_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_jam_decode(Abstractformat *self,
                             const xx_jam_member *member, uint8_t **out,
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
         !xx_jam_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_JAM_MAGIC_SIZE 3
#define XX_JAM_ROOT_OFFSET 3
#define XX_JAM_NAME_SIZE 15
#define XX_JAM_FILE_ENTRY_SIZE 23
#define XX_JAM_DIR_ENTRY_SIZE 19
#define XX_JAM_COUNT_SIZE 4
#define XX_JAM_HEADER_SIZE 30
#define XX_JAM_MAX_MEMBERS 1000000
#define XX_JAM_MAX_DEPTH 32
/* Bounds the visited-node table, and with it both the tree's node count and
 * any single node's subdirectory count. */
#define XX_JAM_MAX_NODES 4096
/* 15 raw bytes, each escaping to at most "%XX", plus a '/' and a NUL. */
#define XX_JAM_NAME_BUFFER (XX_JAM_NAME_SIZE * 3 + 2)

static uint32_t xx_jam_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The reference validator: every byte up to the first NUL must be above 0x20,
 * and everything from the NUL to the end of the 15-byte field must be NUL. A
 * field with no terminator at all is rejected. Together with the count that
 * precedes the table, this is what keeps an arbitrary run of bytes from
 * reading as a directory, so it must not be loosened.
 *
 * Bytes above 0x7E are deliberately permitted: the field is raw DOS bytes and
 * the name builder below escapes anything unsafe as %XX, so nothing
 * unprintable reaches the filesystem. */
static bool xx_jam_name_valid(const uint8_t *raw) {
    size_t index = 0U;

    while (index < (size_t)XX_JAM_NAME_SIZE && raw[index] != 0U) {
        if (raw[index] < 0x21U) return false;
        ++index;
    }
    if (index >= (size_t)XX_JAM_NAME_SIZE) return false;
    for (; index < (size_t)XX_JAM_NAME_SIZE; ++index) {
        if (raw[index] != 0U) return false;
    }
    return true;
}

/* Names are DOS 8.3 identifiers throughout the corpus, but the field is raw
 * bytes: path separators and Windows reserved punctuation are escaped as %XX
 * rather than folded to '_', because escaping is reversible and cannot
 * collapse two distinct members onto one output file. */
static char *xx_jam_join(const char *prefix, const uint8_t *raw,
                         size_t fallback_index, bool trailing_slash) {
    static const char hex_digits[] = "0123456789ABCDEF";
    char escaped[XX_JAM_NAME_BUFFER];
    size_t escaped_length = 0U;
    size_t prefix_length = 0U;
    size_t index;
    char *result;

    for (index = 0U;
         index < (size_t)XX_JAM_NAME_SIZE && raw[index] != 0U; ++index) {
        uint8_t byte = raw[index];
        bool safe = byte > 0x20U && byte < 0x7FU && byte != '%' &&
                    byte != '/' && byte != '\\' && byte != ':' &&
                    byte != '*' && byte != '?' && byte != '"' &&
                    byte != '<' && byte != '>' && byte != '|';

        if (safe) {
            escaped[escaped_length++] = (char)byte;
        } else {
            escaped[escaped_length++] = '%';
            escaped[escaped_length++] = hex_digits[(byte >> 4) & 0x0FU];
            escaped[escaped_length++] = hex_digits[byte & 0x0FU];
        }
    }
    if (escaped_length == 0U) {
        /* An all-NUL field passes the validator, so an unnamed record has to
         * be given a stable synthetic name rather than an empty one. */
        char digits[24];
        size_t digit_count = 0U;
        size_t value = fallback_index;

        escaped[escaped_length++] = 'r';
        escaped[escaped_length++] = 'e';
        escaped[escaped_length++] = 'c';
        escaped[escaped_length++] = 'o';
        escaped[escaped_length++] = 'r';
        escaped[escaped_length++] = 'd';
        do {
            digits[digit_count++] = (char)('0' + (int)(value % 10U));
            value /= 10U;
        } while (value != 0U && digit_count < sizeof(digits));
        while (digit_count > 0U) {
            escaped[escaped_length++] = digits[--digit_count];
        }
    }
    if (trailing_slash) escaped[escaped_length++] = '/';

    if (prefix) {
        while (prefix[prefix_length] != '\0') ++prefix_length;
    }
    result = (char *)xx_mem_alloc(prefix_length + escaped_length + 1U);
    if (!result) return NULL;
    for (index = 0U; index < prefix_length; ++index) {
        result[index] = prefix[index];
    }
    for (index = 0U; index < escaped_length; ++index) {
        result[prefix_length + index] = escaped[index];
    }
    result[prefix_length + escaped_length] = '\0';
    return result;
}

static bool xx_jam_walk(Abstractformat *self, xx_jam_stream *stream,
                        int64_t span, int64_t node_offset, const char *prefix,
                        int64_t *visited, size_t *visited_count, int depth,
                        xx_pd_struct *pd) {
    uint8_t entry[XX_JAM_FILE_ENTRY_SIZE];
    int64_t offset;
    int64_t file_count;
    int64_t dir_count;
    int64_t index;
    size_t scan;

    if (depth > XX_JAM_MAX_DEPTH) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    /* Child node offsets are absolute, so a malformed archive can point a
     * subdirectory back at an ancestor. Without this the walk never ends. */
    for (scan = 0U; scan < *visited_count; ++scan) {
        if (visited[scan] == node_offset) return false;
    }
    if (*visited_count >= (size_t)XX_JAM_MAX_NODES) return false;
    visited[(*visited_count)++] = node_offset;

    if (!xx_jam_range_within(span, node_offset, XX_JAM_COUNT_SIZE) ||
        !xx_jam_read_at(self, self->base_address + node_offset, entry,
                        (size_t)XX_JAM_COUNT_SIZE)) {
        return false;
    }
    file_count = (int64_t)(int32_t)xx_jam_le32(entry);
    if (file_count < 0 || file_count > XX_JAM_MAX_MEMBERS) return false;
    offset = node_offset + XX_JAM_COUNT_SIZE;
    if (!xx_jam_range_within(span, offset,
                             file_count * XX_JAM_FILE_ENTRY_SIZE)) {
        return false;
    }

    for (index = 0; index < file_count; ++index) {
        xx_jam_member member;
        int64_t data_offset;
        int64_t data_size;
        char *name;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (stream->count >= (size_t)XX_JAM_MAX_MEMBERS) return false;
        if (!xx_jam_read_at(self, self->base_address + offset, entry,
                            (size_t)XX_JAM_FILE_ENTRY_SIZE) ||
            !xx_jam_name_valid(entry)) {
            return false;
        }
        data_offset = (int64_t)(int32_t)xx_jam_le32(entry + XX_JAM_NAME_SIZE);
        data_size =
            (int64_t)(int32_t)xx_jam_le32(entry + XX_JAM_NAME_SIZE + 4);
        /* Member data always sits past the magic; an entry pointing into the
         * three-byte header is a directory read out of unrelated bytes. */
        if (data_offset < XX_JAM_MAGIC_SIZE || data_size < 0) return false;
        if (!xx_jam_range_within(span, data_offset, data_size)) return false;

        name = xx_jam_join(prefix, entry, stream->count, false);
        if (!name) return false;
        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_JAM_FILE_ENTRY_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = data_size;
        if (!xx_jam_add(stream, &member)) {
            xx_str_free(name);
            return false;
        }
        offset += XX_JAM_FILE_ENTRY_SIZE;
    }

    if (!xx_jam_range_within(span, offset, XX_JAM_COUNT_SIZE) ||
        !xx_jam_read_at(self, self->base_address + offset, entry,
                        (size_t)XX_JAM_COUNT_SIZE)) {
        return false;
    }
    dir_count = (int64_t)(int32_t)xx_jam_le32(entry);
    if (dir_count < 0 || dir_count > XX_JAM_MAX_NODES) return false;
    offset += XX_JAM_COUNT_SIZE;
    if (!xx_jam_range_within(span, offset,
                             dir_count * XX_JAM_DIR_ENTRY_SIZE)) {
        return false;
    }

    for (index = 0; index < dir_count; ++index) {
        int64_t child_offset;
        char *child_prefix;
        bool walked;

        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!xx_jam_read_at(self, self->base_address + offset, entry,
                            (size_t)XX_JAM_DIR_ENTRY_SIZE) ||
            !xx_jam_name_valid(entry)) {
            return false;
        }
        child_offset = (int64_t)(int32_t)xx_jam_le32(entry + XX_JAM_NAME_SIZE);
        if (child_offset < XX_JAM_MAGIC_SIZE) return false;
        child_prefix = xx_jam_join(prefix, entry, (size_t)index, true);
        if (!child_prefix) return false;
        walked = xx_jam_walk(self, stream, span, child_offset, child_prefix,
                             visited, visited_count, depth + 1, pd);
        xx_str_free(child_prefix);
        if (!walked) return false;
        offset += XX_JAM_DIR_ENTRY_SIZE;
    }
    return true;
}

static xx_jam_stream *xx_jam_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_jam_stream *stream = NULL;
    int64_t *visited = NULL;
    size_t visited_count = 0U;
    uint8_t header[XX_JAM_HEADER_SIZE];
    uint32_t first_offset;
    int64_t first_size;
    int64_t total;
    int64_t span;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Magic, the root file count, one complete file record, and the
     * subdirectory count that must follow it. */
    if (span < XX_JAM_HEADER_SIZE + XX_JAM_COUNT_SIZE) return NULL;
    if (!xx_jam_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (header[0] != 'J' || header[1] != 'A' || header[2] != 'M') return NULL;
    /* The root always holds at least one file, so its table is present and
     * the first record below can be validated. */
    if ((int32_t)xx_jam_le32(header + XX_JAM_ROOT_OFFSET) <= 0) return NULL;

    first_offset = xx_jam_le32(header + 0x16);
    first_size = (int64_t)(int32_t)xx_jam_le32(header + 0x1A);
    /* This is the format's only real defence against a false positive: three
     * magic bytes are cheap to hit by accident, but a writer always starts
     * the data area on a 256-byte boundary, so the first member's offset is a
     * nonzero multiple of 256. Loosening this turns any file beginning "JAM"
     * into a candidate archive. */
    if (first_offset == 0U || first_offset > 0x7FFFFFFFU) return NULL;
    if ((first_offset & 0xFFU) != 0U) return NULL;
    if (first_size <= 0) return NULL;
    if (!xx_jam_name_valid(header + XX_JAM_ROOT_OFFSET + XX_JAM_COUNT_SIZE)) {
        return NULL;
    }

    stream = (xx_jam_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    visited = (int64_t *)xx_mem_alloc(sizeof(*visited) * XX_JAM_MAX_NODES);
    if (!visited) goto fail;

    if (!xx_jam_walk(self, stream, span, XX_JAM_ROOT_OFFSET, "", visited,
                     &visited_count, 0, pd)) {
        goto fail;
    }
    if (stream->count == 0U) goto fail;

    xx_mem_free(visited);
    stream->archive_size = span;
    return stream;

fail:
    xx_mem_free(visited);
    xx_jam_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_jam_init(xx_jam *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_JAM;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-jam");
    xx_format_set_extension(&archive->format, "jam");
    archive->format.check_is_valid = xx_jam_check_is_valid;
    archive->format.handle_base_info = xx_jam_handle_base_info;
    archive->format.get_format_size = xx_jam_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_jam_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_jam_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_jam_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_jam_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_jam_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_jam_free_archive_records_reading;
    archive->format.destroy = xx_jam_vtable_destroy;
}

xx_jam *xx_jam_create(xx_io_device *device, int64_t base_address) {
    xx_jam *archive = (xx_jam *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_jam_init(archive, device, base_address);
    return archive;
}

void xx_jam_destroy(xx_jam *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_jam_free(xx_jam *archive) {
    if (!archive) return;
    xx_jam_destroy(archive);
    xx_mem_free(archive);
}

static void xx_jam_vtable_destroy(Abstractformat *self) {
    xx_jam_destroy((xx_jam *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_jam_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_jam_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_jam_parse(self, pd);
    if (!stream) return false;
    xx_jam_stream_free(stream);
    return true;
}

bool xx_jam_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_jam *archive = (xx_jam *)self;
    xx_jam_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_jam_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_jam_stream_free(stream);
    return true;
}

int64_t xx_jam_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_jam_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_jam *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_jam_set_record(xx_archive_record *record,
                                 const xx_jam_member *member) {
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

static bool xx_jam_copy_options(xx_list_s *target,
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

static const xx_var *xx_jam_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_jam_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_jam_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_jam_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_jam_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_jam_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_jam_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_jam_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_jam_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_jam_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_jam_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_jam_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_jam_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_jam_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_jam_stream *stream;
    const xx_jam_member *member;
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
    stream = (xx_jam_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_jam_path_safe(member->name)) return false;

    path_option = xx_jam_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_jam_decode(self, member, &plain, &plain_size, pd);
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
        !xx_jam_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_jam_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
