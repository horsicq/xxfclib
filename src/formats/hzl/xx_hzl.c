/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HZL single-file compressor output.
 *
 *   0x00  four bytes "!HZL"
 *   0x04  u32 LE uncompressed size (treated as signed: the container writes
 *         a 32-bit value and a negative one is a corrupt field, not a
 *         four-gigabyte member)
 *   0x08  u8  '.', always literally a dot
 *   0x09  three bytes, the original DOS extension, padded with spaces or
 *         NULs when it is shorter than three characters
 *   0x0c  the LZHUF stream, running to end-of-file
 *
 * There is one member, no member table, no CRC and no timestamp anywhere in
 * the container. The compressed size is simply the file size minus twelve.
 *
 * The original base name is not stored - only the extension is - so members
 * are published as a fixed base name with the stored extension appended.
 *
 * The codec is Yoshizaki/Okumura LZHUF in the sub-variant xx_hzl_decode_memory
 * implements (8 KiB ring prefilled with spaces, F = 60, N_CHAR = 314 with NO
 * stop code). Because the stream carries no terminator, the header's size
 * field is the only thing that ends the decode.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hzl/xx_hzl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/hzl/xx_hzl.h"

#include <stdio.h>

#define XX_HZL_COPY_CHUNK (64 * 1024)

typedef struct xx_hzl_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_hzl_member;

typedef struct xx_hzl_stream_s {
    xx_hzl_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_hzl_stream;

static void xx_hzl_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_hzl_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_hzl_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_hzl_path_safe(const char *name) {
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

static void xx_hzl_stream_free(void *pointer) {
    xx_hzl_stream *stream = (xx_hzl_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_hzl_add(xx_hzl_stream *stream,
                          const xx_hzl_member *member) {
    xx_hzl_member *grown = (xx_hzl_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_HZL_BASE_NAME "hzl_data"
#define XX_HZL_BASE_NAME_LENGTH 8
#define XX_HZL_HEADER_SIZE 12
#define XX_HZL_EXTENSION_SIZE 4
#define XX_HZL_MAX_MEMBERS 1
#define XX_HZL_MAX_UNCOMPRESSED ((int64_t)0x40000000)
#define XX_HZL_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_HZL_METHOD_STORE 0U
#define XX_HZL_METHOD_LZHUF 1U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_hzl_le32(const uint8_t *data);
static bool xx_hzl_extension_char(uint8_t character);
static xx_hzl_stream *xx_hzl_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_hzl_decode(Abstractformat *self, const xx_hzl_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The base name is not stored anywhere in the container, only the
 * extension, so this placeholder is the only truthful thing to publish. */

static uint32_t xx_hzl_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* A DOS extension character, or one of the two pad bytes that stand in for a
 * shorter extension. Keeping this set tight is half of what stops a chance
 * "!HZL" from being accepted. */
static bool xx_hzl_extension_char(uint8_t character) {
    if (character >= 'A' && character <= 'Z') return true;
    if (character >= 'a' && character <= 'z') return true;
    if (character >= '0' && character <= '9') return true;
    if (character == '_' || character == '-' || character == '~') return true;
    return character == ' ' || character == 0U;
}

static xx_hzl_stream *xx_hzl_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_hzl_stream *stream;
    xx_hzl_member member;
    uint8_t header[XX_HZL_HEADER_SIZE];
    char buffer[XX_HZL_BASE_NAME_LENGTH + XX_HZL_EXTENSION_SIZE + 1];
    char *name;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    int64_t uncompressed_size;
    size_t length;
    size_t index;
    uint32_t method;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with no payload behind it describes nothing. */
    if (span <= XX_HZL_HEADER_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_hzl_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    if (header[0] != '!' || header[1] != 'H' || header[2] != 'Z' ||
        header[3] != 'L') {
        return NULL;
    }
    /* Four bytes of magic and nothing else would match far too often. The
     * literal dot at offset 8 is the format's only other invariant, and with
     * the three-character extension class behind it, it is what separates
     * "!HZL" from a random four-byte hit. Loosening either check costs the
     * format its entire false-positive defence - there is no checksum, no
     * member table and no terminator to fall back on. */
    if (header[8] != '.') return NULL;
    for (index = 9U; index < (size_t)XX_HZL_HEADER_SIZE; ++index) {
        if (!xx_hzl_extension_char(header[index])) return NULL;
    }

    /* Signed on purpose: the container writes a 32-bit value and a negative
     * one is a corrupt field. */
    uncompressed_size = (int64_t)(int32_t)xx_hzl_le32(header + 4);
    if (uncompressed_size < 0 ||
        uncompressed_size > XX_HZL_MAX_UNCOMPRESSED) {
        return NULL;
    }
    compressed_size = span - XX_HZL_HEADER_SIZE;
    if (!xx_hzl_range_within(span, XX_HZL_HEADER_SIZE, compressed_size)) {
        return NULL;
    }

    if (uncompressed_size == 0) {
        /* A declared plaintext length of zero means the payload was stored,
         * not that the member is empty; the stream bytes are the member, so
         * the two sizes are made equal here rather than publishing a member
         * that claims to hold nothing. */
        method = XX_HZL_METHOD_STORE;
        uncompressed_size = compressed_size;
    } else {
        method = XX_HZL_METHOD_LZHUF;
    }

    /* base name + '.' + up to three extension characters. */
    for (index = 0U; index < (size_t)XX_HZL_BASE_NAME_LENGTH; ++index) {
        buffer[index] = XX_HZL_BASE_NAME[index];
    }
    length = (size_t)XX_HZL_BASE_NAME_LENGTH;
    for (index = 8U; index < (size_t)XX_HZL_HEADER_SIZE; ++index) {
        /* Both pad bytes end the extension; a bare dot is dropped with it. */
        if (header[index] == ' ' || header[index] == 0U) break;
        buffer[length++] = (char)header[index];
    }
    if (length == (size_t)XX_HZL_BASE_NAME_LENGTH + 1U) {
        /* Nothing followed the dot. */
        --length;
    }
    buffer[length] = '\0';

    stream = (xx_hzl_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(buffer);
    if (!name) goto fail;
    if (!xx_hzl_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_HZL_HEADER_SIZE;
    member.data_offset = self->base_address + XX_HZL_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    member.method = method;
    /* The container carries neither a timestamp nor a CRC. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_hzl_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_hzl_stream_free(stream);
    return NULL;
}


/* Exactly one member; the cap exists only for shape. */

/* The container's own field is a signed 32-bit value; this is the reference
 * reader's sanity ceiling on it. */

/* The container has NO method field. These two numbers are derived from the
 * size field, exactly as the reference reader derives its handle method: a
 * declared plaintext length of zero means the payload was never compressed. */

static bool xx_hzl_decode(Abstractformat *self, const xx_hzl_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 1 ||
        member->compressed_size > XX_HZL_MAX_DECODED) {
        return false;
    }
    /* Only the two derived methods exist; anything else means the parse
     * published something this decode does not understand, and treating it
     * as stored would hand the caller garbage. */
    if (member->method != XX_HZL_METHOD_STORE &&
        member->method != XX_HZL_METHOD_LZHUF) {
        return false;
    }
    if (member->uncompressed_size < 1 ||
        member->uncompressed_size > XX_HZL_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_hzl_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_HZL_METHOD_STORE) {
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(input);
            return false;
        }
        *out = input;
        *out_size = (size_t)member->compressed_size;
        return true;
    }

    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!output) {
        xx_mem_free(input);
        return false;
    }
    /* The stream has no terminator, so output_size IS the stop condition:
     * xx_hzl_decode_memory succeeds only when it produced exactly that many
     * bytes, which is what keeps a truncated stream from being reported as a
     * short but successful member. */
    if (!xx_hzl_decode_memory(input, (size_t)member->compressed_size, output,
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

void xx_hzl_init(xx_hzl *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_HZL;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hzl");
    xx_format_set_extension(&archive->format, "hzl");
    archive->format.check_is_valid = xx_hzl_check_is_valid;
    archive->format.handle_base_info = xx_hzl_handle_base_info;
    archive->format.get_format_size = xx_hzl_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_hzl_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_hzl_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_hzl_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_hzl_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_hzl_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_hzl_free_archive_records_reading;
    archive->format.destroy = xx_hzl_vtable_destroy;
}

xx_hzl *xx_hzl_create(xx_io_device *device, int64_t base_address) {
    xx_hzl *archive = (xx_hzl *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_hzl_init(archive, device, base_address);
    return archive;
}

void xx_hzl_destroy(xx_hzl *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_hzl_free(xx_hzl *archive) {
    if (!archive) return;
    xx_hzl_destroy(archive);
    xx_mem_free(archive);
}

static void xx_hzl_vtable_destroy(Abstractformat *self) {
    xx_hzl_destroy((xx_hzl *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_hzl_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_hzl_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_hzl_parse(self, pd);
    if (!stream) return false;
    xx_hzl_stream_free(stream);
    return true;
}

bool xx_hzl_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_hzl *archive = (xx_hzl *)self;
    xx_hzl_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_hzl_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_hzl_stream_free(stream);
    return true;
}

int64_t xx_hzl_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_hzl_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_hzl *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_hzl_set_record(xx_archive_record *record,
                                 const xx_hzl_member *member) {
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

static bool xx_hzl_copy_options(xx_list_s *target,
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

static const xx_var *xx_hzl_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_hzl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_hzl_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_hzl_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_hzl_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_hzl_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_hzl_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_hzl_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_hzl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_hzl_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_hzl_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_hzl_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_hzl_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_hzl_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_hzl_stream *stream;
    const xx_hzl_member *member;
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
    stream = (xx_hzl_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_hzl_path_safe(member->name)) return false;

    path_option = xx_hzl_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_hzl_decode(self, member, &plain, &plain_size, pd);
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
        !xx_hzl_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_hzl_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
