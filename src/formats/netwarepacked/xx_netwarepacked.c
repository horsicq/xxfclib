/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Novell "Packed File" containers (Personal NetWare / Novell DOS).
 *
 *   0x00  twelve bytes, the literal "Packed File " (trailing space included)
 *   0x0c  name, 12 bytes, FIXED WIDTH and NUL padded - a name that fills the
 *         field carries no terminator at all, so all twelve bytes have to be
 *         read
 *   0x18  u8  0x1A, the DOS end-of-file marker
 *   0x19  u8  format version, only 0x01 exists
 *   0x1a  u8  method, only 0x0A exists
 *   0x1b  u32 LE uncompressed size
 *   0x1f  the token stream, running to end-of-file
 *
 * One member, no member table, no CRC and no timestamp. The compressed size
 * is the file size minus 31.
 *
 * The codec is LSB-first: three self-describing Huffman trees (literals,
 * match lengths with a 0xFE escape to a 13-bit length, match-distance high
 * part) followed by the tokens. The declared size is the stream's only end
 * marker, which is why a member whose decode stops short must be reported as
 * a failure rather than as a short read.
 *
 * The name field is not always a name: some packers leave scratch bytes
 * there. A field that does not pass the name rules falls back to a
 * placeholder rather than being published as-is.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/netwarepacked/xx_netwarepacked.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/netwarepack/xx_netwarepack.h"

#include <stdio.h>

#define XX_NETWAREPACKED_COPY_CHUNK (64 * 1024)

typedef struct xx_netwarepacked_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_netwarepacked_member;

typedef struct xx_netwarepacked_stream_s {
    xx_netwarepacked_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_netwarepacked_stream;

static void xx_netwarepacked_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_netwarepacked_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_netwarepacked_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_netwarepacked_path_safe(const char *name) {
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

static void xx_netwarepacked_stream_free(void *pointer) {
    xx_netwarepacked_stream *stream = (xx_netwarepacked_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_netwarepacked_add(xx_netwarepacked_stream *stream,
                          const xx_netwarepacked_member *member) {
    xx_netwarepacked_member *grown = (xx_netwarepacked_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_NETWAREPACKED_FALLBACK_NAME "netware.bin"
#define XX_NETWAREPACKED_HEADER_SIZE 31
#define XX_NETWAREPACKED_NAME_OFFSET 12
#define XX_NETWAREPACKED_NAME_SIZE 12
#define XX_NETWAREPACKED_MAX_MEMBERS 1
#define XX_NETWAREPACKED_EOF_MARKER 0x1AU
#define XX_NETWAREPACKED_VERSION 0x01U
#define XX_NETWAREPACKED_METHOD_LZH 0x0AU
#define XX_NETWAREPACKED_MAX_UNCOMPRESSED ((int64_t)0x7FFFFFFF)
#define XX_NETWAREPACKED_MAX_INPUT ((int64_t)1024 * 1024 * 1024)
#define XX_NETWAREPACKED_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_netwarepacked_le32(const uint8_t *data);
static bool xx_netwarepacked_name_ok(const uint8_t *bytes, size_t length);
static xx_netwarepacked_stream *xx_netwarepacked_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_netwarepacked_decode(Abstractformat *self, const xx_netwarepacked_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Used when the name field holds packer scratch rather than a name. */

static uint32_t xx_netwarepacked_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The name becomes an output file name, so every separator, traversal and
 * control character is rejected here rather than downstream. */
static bool xx_netwarepacked_name_ok(const uint8_t *bytes, size_t length) {
    size_t index;

    if (length < 1U || length > (size_t)XX_NETWAREPACKED_NAME_SIZE) {
        return false;
    }
    if (length == 1U && bytes[0] == '.') return false;
    if (length == 2U && bytes[0] == '.' && bytes[1] == '.') return false;
    for (index = 0U; index < length; ++index) {
        uint8_t character = bytes[index];
        if (character < 0x20U || character >= 0x7FU) return false;
        if (character == '/' || character == '\\' || character == ':' ||
            character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|') {
            return false;
        }
    }
    return true;
}

static xx_netwarepacked_stream *xx_netwarepacked_parse(Abstractformat *self,
                                                       xx_pd_struct *pd) {
    static const char magic[XX_NETWAREPACKED_NAME_OFFSET] = {
        'P', 'a', 'c', 'k', 'e', 'd', ' ', 'F', 'i', 'l', 'e', ' '};
    xx_netwarepacked_stream *stream;
    xx_netwarepacked_member member;
    uint8_t header[XX_NETWAREPACKED_HEADER_SIZE];
    char buffer[XX_NETWAREPACKED_NAME_SIZE + 1];
    const uint8_t *field;
    char *name;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    size_t length;
    size_t index;
    uint8_t version;
    uint8_t method;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with no stream behind it describes nothing. */
    if (span <= XX_NETWAREPACKED_HEADER_SIZE) return NULL;
    if (span > XX_NETWAREPACKED_MAX_INPUT) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_netwarepacked_read_at(self, self->base_address, header,
                                  sizeof(header))) {
        return NULL;
    }

    for (index = 0U; index < (size_t)XX_NETWAREPACKED_NAME_OFFSET; ++index) {
        /* The trailing space is part of the tag; dropping it would match
         * "Packed Files" and similar prose. */
        if (header[index] != (uint8_t)magic[index]) return NULL;
    }
    /* Fifteen fixed bytes in total - the twelve-byte tag, the 0x1A marker and
     * the version/method pair - are this format's entire false-positive
     * defence. There is no checksum, no terminator and no member table, and
     * the codec exposes no bounded probe entry point, so each of these three
     * checks has to stay exactly as strict as it is: accepting a version or
     * method other than 01/0A would hand an arbitrary file to the decoder. */
    if (header[24] != XX_NETWAREPACKED_EOF_MARKER) return NULL;
    version = header[25];
    method = header[26];
    if (version != XX_NETWAREPACKED_VERSION ||
        method != XX_NETWAREPACKED_METHOD_LZH) {
        return NULL;
    }

    uncompressed_size =
        (int64_t)xx_netwarepacked_le32(header + 27);
    if (uncompressed_size > XX_NETWAREPACKED_MAX_UNCOMPRESSED) return NULL;
    compressed_size = span - XX_NETWAREPACKED_HEADER_SIZE;
    if (!xx_netwarepacked_range_within(span, XX_NETWAREPACKED_HEADER_SIZE,
                                       compressed_size)) {
        return NULL;
    }
    /* The declared size is the stream's only end marker, so a member that
     * claims nothing has no stream to decode. */
    if (uncompressed_size < 1) return NULL;

    /* Fixed-width and NUL padded: the name stops at the first NUL, and a
     * field that fills all twelve bytes has no terminator at all. Reading
     * eleven bytes would clip every such name. */
    field = header + XX_NETWAREPACKED_NAME_OFFSET;
    length = 0U;
    while (length < (size_t)XX_NETWAREPACKED_NAME_SIZE && field[length] != 0U) {
        ++length;
    }
    if (xx_netwarepacked_name_ok(field, length)) {
        for (index = 0U; index < length; ++index) {
            buffer[index] = (char)field[index];
        }
        buffer[length] = '\0';
    } else {
        /* Some packers leave scratch bytes in this field. That is not a
         * reason to reject the container, but it is a reason not to publish
         * the bytes as a file name. */
        const char *fallback = XX_NETWAREPACKED_FALLBACK_NAME;
        index = 0U;
        while (fallback[index] != '\0') {
            buffer[index] = fallback[index];
            ++index;
        }
        buffer[index] = '\0';
    }

    stream = (xx_netwarepacked_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(buffer);
    if (!name) goto fail;
    if (!xx_netwarepacked_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_NETWAREPACKED_HEADER_SIZE;
    member.data_offset = self->base_address + XX_NETWAREPACKED_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    /* The container's own method byte, unchanged. */
    member.method = (uint32_t)method;
    /* The container carries neither a timestamp nor a CRC. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_netwarepacked_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_netwarepacked_stream_free(stream);
    return NULL;
}


/* Exactly one member; the cap exists only for shape. */


/* The producer writes the size as a signed 32-bit value, so 0x80000000 and
 * above never appear, and the codec cannot address more output than that. */

static bool xx_netwarepacked_decode(Abstractformat *self,
                                    const xx_netwarepacked_member *member,
                                    uint8_t **out, size_t *out_size,
                                    xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* 01/0A is the only pair that exists. Anything else is a container this
     * decoder cannot read, and treating it as stored would publish garbage
     * at exit status zero. */
    if (member->method != (uint32_t)XX_NETWAREPACKED_METHOD_LZH) return false;
    if (member->compressed_size < 1 ||
        member->compressed_size > XX_NETWAREPACKED_MAX_INPUT) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_NETWAREPACKED_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_netwarepacked_read_at(self, member->data_offset, input,
                                  (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* The declared size is the stream's only end marker, so
     * xx_netwarepack_decode_memory succeeds only when it produced exactly
     * that many bytes; checking `written` again here is what guarantees a
     * caller never receives a partially decoded member as a success. */
    if (!xx_netwarepack_decode_memory(input, (size_t)member->compressed_size,
                                      output,
                                      (size_t)member->uncompressed_size,
                                      &written) ||
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

void xx_netwarepacked_init(xx_netwarepacked *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_NETWAREPACKED;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-netware-packed");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_netwarepacked_check_is_valid;
    archive->format.handle_base_info = xx_netwarepacked_handle_base_info;
    archive->format.get_format_size = xx_netwarepacked_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_netwarepacked_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_netwarepacked_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_netwarepacked_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_netwarepacked_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_netwarepacked_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_netwarepacked_free_archive_records_reading;
    archive->format.destroy = xx_netwarepacked_vtable_destroy;
}

xx_netwarepacked *xx_netwarepacked_create(xx_io_device *device, int64_t base_address) {
    xx_netwarepacked *archive = (xx_netwarepacked *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_netwarepacked_init(archive, device, base_address);
    return archive;
}

void xx_netwarepacked_destroy(xx_netwarepacked *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_netwarepacked_free(xx_netwarepacked *archive) {
    if (!archive) return;
    xx_netwarepacked_destroy(archive);
    xx_mem_free(archive);
}

static void xx_netwarepacked_vtable_destroy(Abstractformat *self) {
    xx_netwarepacked_destroy((xx_netwarepacked *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_netwarepacked_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_netwarepacked_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_netwarepacked_parse(self, pd);
    if (!stream) return false;
    xx_netwarepacked_stream_free(stream);
    return true;
}

bool xx_netwarepacked_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_netwarepacked *archive = (xx_netwarepacked *)self;
    xx_netwarepacked_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_netwarepacked_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_netwarepacked_stream_free(stream);
    return true;
}

int64_t xx_netwarepacked_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_netwarepacked_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_netwarepacked *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_netwarepacked_set_record(xx_archive_record *record,
                                 const xx_netwarepacked_member *member) {
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

static bool xx_netwarepacked_copy_options(xx_list_s *target,
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

static const xx_var *xx_netwarepacked_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_netwarepacked_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_netwarepacked_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_netwarepacked_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_netwarepacked_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_netwarepacked_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_netwarepacked_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_netwarepacked_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_netwarepacked_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_netwarepacked_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_netwarepacked_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_netwarepacked_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_netwarepacked_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_netwarepacked_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_netwarepacked_stream *stream;
    const xx_netwarepacked_member *member;
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
    stream = (xx_netwarepacked_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_netwarepacked_path_safe(member->name)) return false;

    path_option = xx_netwarepacked_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_netwarepacked_decode(self, member, &plain, &plain_size, pd);
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
        !xx_netwarepacked_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_netwarepacked_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
