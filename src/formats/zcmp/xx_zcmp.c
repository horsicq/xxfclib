/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Zcmp compressed files (the Solaris "compressed file" wrapper).
 *
 *   header, 40 bytes, BIG-endian:
 *     0x00  u32  0x00000000, always
 *     0x04  4    ASCII "Zcmp"
 *     0x08  u64  1
 *     0x10  u64  1
 *     0x18  u64  uncompressed size of the payload
 *     0x20  u64  block size: the plaintext each block expands to, except the
 *                last one
 *
 *   seek index, (block_count + 1) * 8 bytes at 0x28, where
 *   block_count = ceil(uncompressed_size / block_size).  It is skipped, never
 *   read: the payload is walked by inflating, so the index is metadata rather
 *   than something this reader has to trust.
 *
 *   payload, from the end of the index to end of file: block_count complete
 *   zlib streams (RFC 1950 - two-byte header, deflate data, Adler-32) laid
 *   BACK TO BACK, each inflating to exactly block_size bytes except the last.
 *
 * TRAP - THERE ARE NO LENGTHS IN FRONT OF THE STREAMS.  A block ends where its
 * deflate data ends and the next one begins at the very next byte, so a
 * boundary can only be found by inflating and reading back how much input the
 * stream consumed.  Handing the whole payload to a one-shot zlib decoder stops
 * at the end of the first block and reports the first block's size as the
 * answer, which is why xx_zcmp_decode_memory exists.
 *
 * The container stores no member name, no timestamp and no method field: it
 * wraps exactly one unnamed stream.  The reference reader borrows the archive
 * file's own name; a device has none here, so the member is called "data".
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zcmp/xx_zcmp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/zcmp/xx_zcmp.h"

#include <stdio.h>

#define XX_ZCMP_COPY_CHUNK (64 * 1024)

typedef struct xx_zcmp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_zcmp_member;

typedef struct xx_zcmp_stream_s {
    xx_zcmp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zcmp_stream;

static void xx_zcmp_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zcmp_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_zcmp_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zcmp_path_safe(const char *name) {
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

static void xx_zcmp_stream_free(void *pointer) {
    xx_zcmp_stream *stream = (xx_zcmp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_zcmp_add(xx_zcmp_stream *stream,
                          const xx_zcmp_member *member) {
    xx_zcmp_member *grown = (xx_zcmp_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ZCMP_HEADER_SIZE 40
#define XX_ZCMP_SIGNATURE_OFFSET 4
#define XX_ZCMP_SIGNATURE_SIZE 4
#define XX_ZCMP_UNCOMPRESSED_OFFSET 0x18
#define XX_ZCMP_BLOCKSIZE_OFFSET 0x20
#define XX_ZCMP_INDEX_ENTRY_SIZE 8
#define XX_ZCMP_MAX_BLOCKS 4000000
#define XX_ZCMP_MAX_OUTPUT 0x7fffffff
#define XX_ZCMP_MAX_DECODED (256 * 1024 * 1024)
#define XX_ZCMP_MAX_MEMBERS 1
#define XX_ZCMP_METHOD_ZLIB_BLOCKS 8U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_zcmp_be32(const uint8_t *data);
static uint64_t xx_zcmp_be64(const uint8_t *data);
static xx_zcmp_stream *xx_zcmp_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_zcmp_decode(Abstractformat *self, const xx_zcmp_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Sizes are 64-bit fields; the reference caps both at INT32_MAX. */
/* One wrapper, one payload, one member. */
/* The container names no method.  The number a listing should show is the
 * well-known deflate id, since every block is a zlib-wrapped deflate stream;
 * it is not a library enum. */

static uint32_t xx_zcmp_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint64_t xx_zcmp_be64(const uint8_t *data) {
    return ((uint64_t)xx_zcmp_be32(data) << 32) |
           (uint64_t)xx_zcmp_be32(data + 4);
}

static xx_zcmp_stream *xx_zcmp_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t signature[XX_ZCMP_SIGNATURE_SIZE] = {'Z', 'c', 'm',
                                                              'p'};
    xx_zcmp_stream *stream = NULL;
    xx_zcmp_member member;
    uint8_t header[XX_ZCMP_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t uncompressed;
    int64_t block_size;
    int64_t block_count;
    int64_t index_size;
    int64_t data_offset;
    int64_t compressed;
    char *name;

    if (!self || !self->device || self->base_address < 0) return NULL;
    /* One member, so the single cancellation check belongs here. */
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_ZCMP_HEADER_SIZE) return NULL;
    if (!xx_zcmp_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* Four zero bytes then "Zcmp" is the searchable signature, and the two
     * u64 constants behind it are the rest of the gate.  They are fixed
     * version words, not free fields: without them any file that happens to
     * carry 00 00 00 00 "Zcmp" at its start - a plausible pattern in a
     * zero-padded table - would be parsed with attacker-chosen sizes, and the
     * size checks below are all that would stand between that and a bogus
     * member. */
    if (xx_zcmp_be32(header) != 0U) return NULL;
    if (xx_rt_memcmp(header + XX_ZCMP_SIGNATURE_OFFSET, signature,
                     XX_ZCMP_SIGNATURE_SIZE) != 0) {
        return NULL;
    }
    if (xx_zcmp_be64(header + 8) != (uint64_t)1) return NULL;
    if (xx_zcmp_be64(header + 16) != (uint64_t)1) return NULL;

    uncompressed = (int64_t)xx_zcmp_be64(header + XX_ZCMP_UNCOMPRESSED_OFFSET);
    block_size = (int64_t)xx_zcmp_be64(header + XX_ZCMP_BLOCKSIZE_OFFSET);
    if (uncompressed < 0 || uncompressed > XX_ZCMP_MAX_OUTPUT) return NULL;
    /* A zero block size would make the block count divide by zero and is not
     * something the writer emits. */
    if (block_size <= 0 || block_size > XX_ZCMP_MAX_OUTPUT) return NULL;

    block_count = (uncompressed + block_size - 1) / block_size;
    if (block_count < 0 || block_count > XX_ZCMP_MAX_BLOCKS) return NULL;

    /* The index is one entry per block plus a terminating entry.  It is
     * skipped rather than read, but its size still has to fit: a header
     * claiming an index larger than the file is the cheapest way to tell a
     * crafted 40 bytes from a real container. */
    index_size = (block_count + 1) * XX_ZCMP_INDEX_ENTRY_SIZE;
    if (!xx_zcmp_range_within(span, XX_ZCMP_HEADER_SIZE, index_size)) {
        return NULL;
    }
    data_offset = XX_ZCMP_HEADER_SIZE + index_size;
    compressed = span - data_offset;
    if (!xx_zcmp_range_within(span, data_offset, compressed)) return NULL;
    /* Blocks were promised but no payload follows them. */
    if (block_count > 0 && compressed <= 0) return NULL;

    stream = (xx_zcmp_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* The container stores no name; the reference borrows the archive file's
     * own, which a device does not have. */
    name = xx_str_dup("data");
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    /* The header part covers the seek index too: it is metadata that is
     * deliberately skipped, not payload. */
    member.header_offset = self->base_address;
    member.header_size = data_offset;
    member.data_offset = self->base_address + data_offset;
    member.compressed_size = compressed;
    member.uncompressed_size = uncompressed;
    member.method = XX_ZCMP_METHOD_ZLIB_BLOCKS;
    /* No timestamp field exists anywhere in the container. */
    member.timestamp = 0U;
    member.is_folder = false;
    if (!xx_zcmp_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count > (size_t)XX_ZCMP_MAX_MEMBERS) goto fail;

    /* The payload runs to EOF, so the container is the whole file. */
    stream->archive_size = span;
    return stream;

fail:
    xx_zcmp_stream_free(stream);
    return NULL;
}


static bool xx_zcmp_decode(Abstractformat *self, const xx_zcmp_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t packed_size;
    size_t plain_size;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    /* The container has no method field; the header's fixed constants assert
     * zlib blocks and nothing else.  A member carrying any other value did not
     * come from this parse and must not be copied out as stored bytes. */
    if (member->method != XX_ZCMP_METHOD_ZLIB_BLOCKS) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if ((uint64_t)member->uncompressed_size > (uint64_t)XX_ZCMP_MAX_DECODED ||
        (uint64_t)member->compressed_size > (uint64_t)XX_ZCMP_MAX_DECODED) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    plain_size = (size_t)member->uncompressed_size;
    packed_size = (size_t)member->compressed_size;
    if (plain_size == 0U) {
        /* A zero-length payload has zero blocks, so there is nothing to
         * inflate; the decoder would refuse a zero output capacity. */
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }
    if (packed_size == 0U) return false;

    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    if (!xx_zcmp_read_at(self, member->data_offset, packed, packed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* The declared size is the walk's terminator: the decoder consumes one
     * zlib stream after another until it has produced exactly this many bytes,
     * and fails if a stream ends early or the total overshoots.  Trailing
     * input after the last needed block is tolerated, as in the reference. */
    if (!xx_zcmp_decode_memory(packed, packed_size, plain, plain_size,
                               &written) ||
        written != plain_size) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_zcmp_init(xx_zcmp *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_ZCMP;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zcmp");
    xx_format_set_extension(&archive->format, "z");
    archive->format.check_is_valid = xx_zcmp_check_is_valid;
    archive->format.handle_base_info = xx_zcmp_handle_base_info;
    archive->format.get_format_size = xx_zcmp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zcmp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zcmp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zcmp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zcmp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zcmp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zcmp_free_archive_records_reading;
    archive->format.destroy = xx_zcmp_vtable_destroy;
}

xx_zcmp *xx_zcmp_create(xx_io_device *device, int64_t base_address) {
    xx_zcmp *archive = (xx_zcmp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zcmp_init(archive, device, base_address);
    return archive;
}

void xx_zcmp_destroy(xx_zcmp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_zcmp_free(xx_zcmp *archive) {
    if (!archive) return;
    xx_zcmp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zcmp_vtable_destroy(Abstractformat *self) {
    xx_zcmp_destroy((xx_zcmp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_zcmp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zcmp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zcmp_parse(self, pd);
    if (!stream) return false;
    xx_zcmp_stream_free(stream);
    return true;
}

bool xx_zcmp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zcmp *archive = (xx_zcmp *)self;
    xx_zcmp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_zcmp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_zcmp_stream_free(stream);
    return true;
}

int64_t xx_zcmp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zcmp_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zcmp *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zcmp_set_record(xx_archive_record *record,
                                 const xx_zcmp_member *member) {
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

static bool xx_zcmp_copy_options(xx_list_s *target,
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

static const xx_var *xx_zcmp_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_zcmp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zcmp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zcmp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zcmp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zcmp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zcmp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_zcmp_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zcmp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zcmp_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_zcmp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zcmp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_zcmp_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zcmp_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_zcmp_stream *stream;
    const xx_zcmp_member *member;
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
    stream = (xx_zcmp_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zcmp_path_safe(member->name)) return false;

    path_option = xx_zcmp_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_zcmp_decode(self, member, &plain, &plain_size, pd);
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
        !xx_zcmp_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_zcmp_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
