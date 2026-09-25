/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MI10 crunched block chains.
 *
 *   block header, 16 bytes, big-endian:
 *     0x00  4 bytes  magic, "MI10"
 *     0x04  u32 BE   checksum, the plain byte sum of the decoded block
 *     0x08  u32 BE   uncompressed size
 *     0x0c  u32 BE   declared packed size, NOT counting the 2-byte stream
 *                    prefix that follows the header
 *
 *   payload: declared_packed_size + 2 bytes immediately after the header.
 *     0x00  u8       fixed 0 marker, invariant in every known stream
 *     0x01  u8       escape byte for this block
 *     0x02..         token stream, decoded BACKWARDS from its last byte
 *
 * Between two blocks an encoder may emit zero, one or two 0x6b pad bytes
 * (and may emit them after the final block as well). Every skipped byte must
 * be an authenticated 0x6b: there is deliberately no loose search for the
 * next magic, because that would turn a corrupt chain into a plausible one.
 *
 * Block headers are always at an even offset. There is no member count and
 * no central directory, so the chain tiling the container exactly is the
 * format's principal structural self-check. Members carry no names; each is
 * listed as "<n>.bin" with n counted from one.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mi10/xx_mi10.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/mi10/xx_mi10.h"

#include <stdio.h>

#define XX_MI10_COPY_CHUNK (64 * 1024)

typedef struct xx_mi10_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_mi10_member;

typedef struct xx_mi10_stream_s {
    xx_mi10_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_mi10_stream;

static void xx_mi10_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_mi10_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_mi10_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_mi10_path_safe(const char *name) {
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

static void xx_mi10_stream_free(void *pointer) {
    xx_mi10_stream *stream = (xx_mi10_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_mi10_add(xx_mi10_stream *stream,
                          const xx_mi10_member *member) {
    xx_mi10_member *grown = (xx_mi10_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_MI10_MAX_MEMBERS 100000
#define XX_MI10_NAME_BUFFER 32
#define XX_MI10_HEADER_SIZE 16
#define XX_MI10_STREAM_PREFIX_SIZE 2
#define XX_MI10_MAX_PADDING 2
#define XX_MI10_PAD_BYTE 0x6bU
#define XX_MI10_METHOD_LZ 0U
#define XX_MI10_MAX_COMPRESSED ((int64_t)0x7fffffff)
#define XX_MI10_MAX_DECODED ((int64_t)0x10000000)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_mi10_be32(const uint8_t *data);
static xx_mi10_stream *xx_mi10_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_mi10_decode(Abstractformat *self, const xx_mi10_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* No block count is stored, so this is a runaway guard, not a format
 * limit. */

/* "9999999999.bin" and a terminator. */

static uint32_t xx_mi10_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static xx_mi10_stream *xx_mi10_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[4] = {(uint8_t)'M', (uint8_t)'I', (uint8_t)'1',
                                     (uint8_t)'0'};
    xx_mi10_stream *stream = NULL;
    uint8_t header[XX_MI10_HEADER_SIZE];
    uint8_t prefix[XX_MI10_STREAM_PREFIX_SIZE];
    uint8_t pad;
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* One header, its stream prefix, and at least one token byte. */
    if (span < XX_MI10_HEADER_SIZE + XX_MI10_STREAM_PREFIX_SIZE + 1) {
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) return NULL;

    stream = (xx_mi10_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = 0;
    while (offset < span) {
        xx_mi10_member member;
        char buffer[XX_MI10_NAME_BUFFER];
        char *name;
        uint32_t checksum;
        uint32_t uncompressed;
        uint32_t declared;
        int64_t data_offset;
        int64_t data_size;
        int64_t next;
        int64_t padding;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (count >= XX_MI10_MAX_MEMBERS) goto fail;
        /* Every block header is word aligned; an odd chain offset means the
         * walk has drifted and must not be repaired by searching. */
        if ((offset & 1) != 0) goto fail;
        if (XX_MI10_HEADER_SIZE > span - offset) goto fail;
        if (!xx_mi10_read_at(self, self->base_address + offset, header,
                             sizeof(header))) {
            goto fail;
        }
        if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) goto fail;

        checksum = xx_mi10_be32(header + 4);
        uncompressed = xx_mi10_be32(header + 8);
        declared = xx_mi10_be32(header + 12);
        if (uncompressed == 0U || declared == 0U) goto fail;
        if ((int64_t)uncompressed > XX_MI10_MAX_COMPRESSED ||
            (int64_t)declared >
                XX_MI10_MAX_COMPRESSED - XX_MI10_STREAM_PREFIX_SIZE) {
            goto fail;
        }
        /* The checksum is a plain sum of the decoded bytes, so it cannot
         * exceed 255 per byte. Cheap, and it rejects most random 16 bytes
         * that happen to start with the printable magic. */
        if ((uint64_t)checksum > (uint64_t)uncompressed * 255ULL) goto fail;

        data_offset = offset + XX_MI10_HEADER_SIZE;
        data_size = (int64_t)declared + XX_MI10_STREAM_PREFIX_SIZE;
        /* A block running past EOF is a rejection, not a short read. */
        if (!xx_mi10_range_within(span, data_offset, data_size)) goto fail;

        if (!xx_mi10_read_at(self, self->base_address + data_offset, prefix,
                             sizeof(prefix))) {
            goto fail;
        }
        /* The leading 0 marker is invariant in every known stream and is the
         * single strongest discriminator this format has beyond the four
         * printable magic bytes. Loosening it makes "MI10" text files
         * match. */
        if (prefix[0] != 0U) goto fail;

        if (xx_rt_snprintf(buffer, sizeof(buffer), "%u.bin",
                           (unsigned)(count + 1)) <= 0) {
            goto fail;
        }
        name = xx_str_dup(buffer);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_MI10_HEADER_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = data_size;
        member.uncompressed_size = (int64_t)uncompressed;
        member.method = XX_MI10_METHOD_LZ;
        /* No timestamp anywhere in the container. */
        member.timestamp = 0U;
        member.is_folder = false;

        if (!xx_mi10_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        ++count;

        next = data_offset + data_size;
        if (next == span) {
            offset = next;
            break;
        }

        /* Zero, one or two 0x6b pad bytes may separate blocks. Each skipped
         * byte must actually BE 0x6b: scanning forward for the next magic
         * instead would let a corrupt or unrelated stretch of bytes pass as
         * inter-block slack. */
        padding = 0;
        while (next < span && padding < XX_MI10_MAX_PADDING) {
            if (!xx_mi10_read_at(self, self->base_address + next, &pad, 1U)) {
                goto fail;
            }
            if (pad != XX_MI10_PAD_BYTE) break;
            ++next;
            ++padding;
        }
        if (next == span) {
            offset = next;
            break;
        }
        /* The next header must be aligned, whole, and carry the magic; the
         * chain is never allowed to resynchronise. */
        if ((next & 1) != 0 || (span - next) < XX_MI10_HEADER_SIZE) goto fail;
        offset = next;
    }

    /* No count and no terminator: the chain has to consume the container
     * exactly. This is the check a later reader will be tempted to loosen so
     * that "an MI10 block with junk after it" still lists, and loosening it
     * makes the format match any file with "MI10" at offset 0. */
    if (offset != span) goto fail;
    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_mi10_stream_free(stream);
    return NULL;
}


/* The 0 marker and the escape byte sit in front of the token stream and are
 * part of what the decoder is handed, but the header's packed size counts
 * only the tokens. */

/* The container has no method field: every block is the MI10 LZ stream. Zero
 * is the only value parse publishes and decode refuses anything else, so a
 * later method number cannot be silently decoded as this one. */

/* Both sizes are stored as u32 but the reference caps them at INT32_MAX, so
 * these bound a corrupt field rather than a real archive. */

/* One backward LZ stream per block. The decoder is output-driven: it stops at
 * output_size and additionally insists the token cursor land exactly on
 * offset 2, so a stream that would decode short is reported as a failure
 * rather than as a partially filled buffer. */
static bool xx_mi10_decode(Abstractformat *self, const xx_mi10_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_MI10_METHOD_LZ) return false;
    /* The prefix plus at least one token byte; the decoder demands 3. */
    if (member->compressed_size < XX_MI10_STREAM_PREFIX_SIZE + 1 ||
        member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_MI10_MAX_COMPRESSED ||
        member->uncompressed_size > XX_MI10_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_mi10_read_at(self, member->data_offset, input,
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
    /* The whole payload including the 0 marker and the escape byte is what
     * the decoder expects as input[0] and input[1]; trimming the prefix here
     * would leave the decoder with no escape byte. */
    if (!xx_mi10_decode_memory(input, (size_t)member->compressed_size, output,
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

void xx_mi10_init(xx_mi10 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_MI10;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mi10");
    xx_format_set_extension(&archive->format, "mi10");
    archive->format.check_is_valid = xx_mi10_check_is_valid;
    archive->format.handle_base_info = xx_mi10_handle_base_info;
    archive->format.get_format_size = xx_mi10_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mi10_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mi10_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mi10_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mi10_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mi10_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mi10_free_archive_records_reading;
    archive->format.destroy = xx_mi10_vtable_destroy;
}

xx_mi10 *xx_mi10_create(xx_io_device *device, int64_t base_address) {
    xx_mi10 *archive = (xx_mi10 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_mi10_init(archive, device, base_address);
    return archive;
}

void xx_mi10_destroy(xx_mi10 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_mi10_free(xx_mi10 *archive) {
    if (!archive) return;
    xx_mi10_destroy(archive);
    xx_mem_free(archive);
}

static void xx_mi10_vtable_destroy(Abstractformat *self) {
    xx_mi10_destroy((xx_mi10 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_mi10_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_mi10_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_mi10_parse(self, pd);
    if (!stream) return false;
    xx_mi10_stream_free(stream);
    return true;
}

bool xx_mi10_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mi10 *archive = (xx_mi10 *)self;
    xx_mi10_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_mi10_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_mi10_stream_free(stream);
    return true;
}

int64_t xx_mi10_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_mi10_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_mi10 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_mi10_set_record(xx_archive_record *record,
                                 const xx_mi10_member *member) {
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

static bool xx_mi10_copy_options(xx_list_s *target,
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

static const xx_var *xx_mi10_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_mi10_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_mi10_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_mi10_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mi10_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_mi10_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_mi10_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_mi10_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_mi10_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_mi10_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_mi10_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_mi10_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_mi10_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_mi10_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_mi10_stream *stream;
    const xx_mi10_member *member;
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
    stream = (xx_mi10_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_mi10_path_safe(member->name)) return false;

    path_option = xx_mi10_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_mi10_decode(self, member, &plain, &plain_size, pd);
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
        !xx_mi10_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_mi10_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
