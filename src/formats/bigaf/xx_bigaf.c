/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * AIX big archives ("<bigaf>\n", the 64-bit successor to "<bigaf>"'s
 * 32-bit sibling).
 *
 * Every numeric field is decimal ASCII, blank padded, and an all-blank field
 * is a legitimate zero -- there is not a single binary integer in the
 * container.
 *
 * File header, 128 bytes at offset 0:
 *
 *   0x00..0x07  magic "<bigaf>\n"
 *   0x08..0x43  member-table / free-list / symbol-table offsets, unused here
 *   0x44 (68)   offset of the first member header, 20 bytes decimal
 *   0x58 (88)   offset of the last member header, 20 bytes decimal
 *   0x6C (108)  remaining header fields, unused here
 *
 * Member header, 112 bytes, at the offset the chain points to:
 *
 *   0x00 (0)    payload size, 20 bytes decimal
 *   0x14 (20)   offset of the NEXT member header, 20 bytes decimal
 *   0x28 (40)   previous offset, date, uid, gid, mode -- unused here
 *   0x6C (108)  name length, 4 bytes decimal
 *   0x70 (112)  the name itself, name-length bytes
 *
 * After the name come two terminator bytes, and the payload then starts at
 * the next even offset. Members are always stored, never compressed.
 *
 * The directory is a linked list rather than a table: there is no member
 * count anywhere, so the walk ends when the chain reaches the offset the file
 * header names as the last member, or when a next-offset of zero says the
 * chain is over.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bigaf/xx_bigaf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_BIGAF_COPY_CHUNK (64 * 1024)

typedef struct xx_bigaf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_bigaf_member;

typedef struct xx_bigaf_stream_s {
    xx_bigaf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_bigaf_stream;

static void xx_bigaf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_bigaf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_bigaf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_bigaf_path_safe(const char *name) {
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

static void xx_bigaf_stream_free(void *pointer) {
    xx_bigaf_stream *stream = (xx_bigaf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_bigaf_add(xx_bigaf_stream *stream,
                          const xx_bigaf_member *member) {
    xx_bigaf_member *grown = (xx_bigaf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


/* Every member is stored verbatim, so extraction is a bounded copy. */
static bool xx_bigaf_decode(Abstractformat *self,
                             const xx_bigaf_member *member, uint8_t **out,
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
         !xx_bigaf_read_at(self, member->data_offset, buffer,
                            (size_t)member->compressed_size))) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->compressed_size;
    return true;
}


#define XX_BIGAF_HEADER_SIZE 128
#define XX_BIGAF_MEMBER_HEADER 112
#define XX_BIGAF_MAX_MEMBERS 100000
#define XX_BIGAF_MAX_NAME 4096

/* Every numeric field is decimal ASCII, blank padded; an all-blank field is a
 * legitimate zero. No sign is accepted: a negative size or offset is never
 * legitimate here, and accepting one would only push the rejection further
 * down into the range checks. */
static bool xx_bigaf_number(const uint8_t *data, size_t size, int64_t *value) {
    size_t start = 0U;
    size_t end = size;
    int64_t result = 0;

    while ((start < end) &&
           ((data[start] == ' ') || (data[start] == '\t'))) {
        ++start;
    }
    while ((end > start) &&
           ((data[end - 1U] == ' ') || (data[end - 1U] == '\t') ||
            (data[end - 1U] == '\n') || (data[end - 1U] == '\r'))) {
        --end;
    }
    if (start == end) {
        *value = 0;
        return true;
    }
    for (; start < end; ++start) {
        uint8_t digit = data[start];

        if ((digit < (uint8_t)'0') || (digit > (uint8_t)'9')) return false;
        /* A 20-character field has room for more than int64_t holds. */
        if (result > (int64_t)922337203685477580) return false;
        result = (result * 10) + (int64_t)(digit - (uint8_t)'0');
    }
    *value = result;
    return true;
}

/* The name is a fixed-length field, not a C string: AIX pads it with blanks,
 * some writers pad with NULs, and bytes above 0x7E are legal because the name
 * is whatever the creating locale produced. So, unlike most containers here,
 * high bytes are deliberately NOT a rejection -- only control bytes are.
 * Returns the usable length, or 0 when the field is implausible. */
static size_t xx_bigaf_name_length(const uint8_t *data, size_t size) {
    size_t length = 0U;
    size_t index;

    for (index = 0U; index < size; ++index) {
        if ((data[index] < 0x20U) && (data[index] != 0U)) return 0U;
    }
    while ((length < size) && (data[length] != 0U)) ++length;
    while ((length > 0U) && (data[length - 1U] == (uint8_t)' ')) --length;
    return length;
}

static xx_bigaf_stream *xx_bigaf_parse(Abstractformat *self,
                                       xx_pd_struct *pd) {
    xx_bigaf_stream *stream;
    uint8_t header[XX_BIGAF_HEADER_SIZE];
    uint8_t entry[XX_BIGAF_MEMBER_HEADER];
    int64_t total;
    int64_t span;
    int64_t first;
    int64_t last;
    int64_t offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_BIGAF_HEADER_SIZE) return NULL;
    if (!xx_bigaf_read_at(self, self->base_address, header, sizeof(header)) ||
        header[0] != '<' || header[1] != 'b' || header[2] != 'i' ||
        header[3] != 'g' || header[4] != 'a' || header[5] != 'f' ||
        header[6] != '>' || header[7] != '\n') {
        return NULL;
    }
    /* The magic alone is weak identification -- it is printable text that can
     * occur in a document. What actually separates a big archive from a file
     * that merely contains the string is that these two 20-byte fields must
     * be decimal ASCII and must name a first member that lies inside the
     * file; loosening either turns the walk below into a scan of arbitrary
     * bytes. */
    if (!xx_bigaf_number(header + 68, 20U, &first)) return NULL;
    if (!xx_bigaf_number(header + 88, 20U, &last)) return NULL;
    if ((first <= 0) || (first >= span)) return NULL;

    stream = (xx_bigaf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = first;
    while (offset > 0) {
        xx_bigaf_member member;
        uint8_t *name_field = NULL;
        char *name;
        int64_t size;
        int64_t next;
        int64_t name_length;
        int64_t data_offset;
        size_t usable;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_BIGAF_MAX_MEMBERS) break;
        if (!xx_bigaf_range_within(span, offset, XX_BIGAF_MEMBER_HEADER) ||
            !xx_bigaf_read_at(self, self->base_address + offset, entry,
                              sizeof(entry))) {
            goto fail;
        }
        if (!xx_bigaf_number(entry, 20U, &size) ||
            !xx_bigaf_number(entry + 20U, 20U, &next) ||
            !xx_bigaf_number(entry + 108U, 4U, &name_length)) {
            goto fail;
        }
        if ((name_length <= 0) || (name_length > XX_BIGAF_MAX_NAME)) goto fail;
        if (!xx_bigaf_range_within(span, offset + XX_BIGAF_MEMBER_HEADER,
                                   name_length)) {
            goto fail;
        }

        name_field = (uint8_t *)xx_mem_alloc((size_t)name_length);
        if (!name_field) goto fail;
        if (!xx_bigaf_read_at(self,
                              self->base_address + offset +
                                  XX_BIGAF_MEMBER_HEADER,
                              name_field, (size_t)name_length)) {
            xx_mem_free(name_field);
            goto fail;
        }
        usable = xx_bigaf_name_length(name_field, (size_t)name_length);
        if (usable == 0U) {
            xx_mem_free(name_field);
            goto fail;
        }
        name = (char *)xx_mem_alloc(usable + 1U);
        if (!name) {
            xx_mem_free(name_field);
            goto fail;
        }
        for (data_offset = 0; data_offset < (int64_t)usable; ++data_offset) {
            name[data_offset] = (char)name_field[data_offset];
        }
        name[usable] = '\0';
        xx_mem_free(name_field);

        /* The name is followed by a two-byte terminator and the payload then
         * starts on an even offset. */
        data_offset = offset + XX_BIGAF_MEMBER_HEADER + name_length + 2;
        if (data_offset & 1) ++data_offset;
        if (!xx_bigaf_range_within(span, data_offset, size)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = data_offset - offset;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = size;
        member.uncompressed_size = size;
        if (!xx_bigaf_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        if (offset == last) break;
        /* The chain has no length field, so nothing in the container itself
         * stops a next-offset that points backwards or at the member just
         * parsed: that is an endless walk, not an archive. Requiring the
         * chain to move strictly forward is what makes termination a property
         * of the parser rather than of the input. */
        if ((next != 0) && (next <= offset)) goto fail;
        offset = next;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_bigaf_stream_free(stream);
    return NULL;
}


/* ---------------------------------------------------------- lifecycle --- */

void xx_bigaf_init(xx_bigaf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_BIGAF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-archive");
    xx_format_set_extension(&archive->format, "a");
    archive->format.check_is_valid = xx_bigaf_check_is_valid;
    archive->format.handle_base_info = xx_bigaf_handle_base_info;
    archive->format.get_format_size = xx_bigaf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bigaf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bigaf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bigaf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bigaf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bigaf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bigaf_free_archive_records_reading;
    archive->format.destroy = xx_bigaf_vtable_destroy;
}

xx_bigaf *xx_bigaf_create(xx_io_device *device, int64_t base_address) {
    xx_bigaf *archive = (xx_bigaf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_bigaf_init(archive, device, base_address);
    return archive;
}

void xx_bigaf_destroy(xx_bigaf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_bigaf_free(xx_bigaf *archive) {
    if (!archive) return;
    xx_bigaf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_bigaf_vtable_destroy(Abstractformat *self) {
    xx_bigaf_destroy((xx_bigaf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_bigaf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_bigaf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_bigaf_parse(self, pd);
    if (!stream) return false;
    xx_bigaf_stream_free(stream);
    return true;
}

bool xx_bigaf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_bigaf *archive = (xx_bigaf *)self;
    xx_bigaf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_bigaf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_bigaf_stream_free(stream);
    return true;
}

int64_t xx_bigaf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_bigaf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_bigaf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_bigaf_set_record(xx_archive_record *record,
                                 const xx_bigaf_member *member) {
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

static bool xx_bigaf_copy_options(xx_list_s *target,
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

static const xx_var *xx_bigaf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_bigaf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_bigaf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_bigaf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_bigaf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_bigaf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_bigaf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_bigaf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_bigaf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_bigaf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_bigaf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_bigaf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_bigaf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_bigaf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_bigaf_stream *stream;
    const xx_bigaf_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_bigaf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_bigaf_path_safe(member->name)) return false;

    path_option = xx_bigaf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_bigaf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_bigaf_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
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
    if (!result) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_bigaf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
