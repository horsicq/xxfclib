/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PowerArc archives.
 *
 *   header, 8 bytes:
 *     0x00  8 bytes  ASCII magic "BZIP0001"
 *
 *   payload, from 0x08 to end of file:
 *     0x08  3 bytes  bzip2 stream magic "BZh"
 *     0x0B  1 byte   bzip2 block-size digit, '1'..'9'
 *     0x0C  6 bytes  48-bit big-endian block magic: 0x314159265359 for a
 *                    data block, or 0x177245385090 for the end-of-stream
 *                    marker of an archive whose payload compressed to
 *                    nothing
 *     ...            remaining bzip2 blocks, EOS marker and combined CRC
 *
 * There is no member table, no method field, no timestamp and no length
 * field: the format is a one-member container and the payload is always
 * bzip2, which the magic itself asserts. The member's uncompressed size is
 * therefore unknown until the stream is decoded, so building a record decodes
 * it once and reports the measured length.
 *
 * The reference implementation names the single member after the file it came
 * from (dropping a trailing ".pk"); a device here has no name, so the member
 * is called "data".
 *
 * Identification rests on the eight-byte magic plus the bzip2 header that
 * must directly follow it. The magic alone is a strong signature, but it is
 * also the only thing separating this format from any other file that happens
 * to begin with those letters, so the bzip2 header behind it -- signature,
 * block-size digit and a well-formed 48-bit block magic -- is checked too.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/powerarc/xx_powerarc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"

#include <stdio.h>

#define XX_POWERARC_COPY_CHUNK (64 * 1024)

typedef struct xx_powerarc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_powerarc_member;

typedef struct xx_powerarc_stream_s {
    xx_powerarc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_powerarc_stream;

static void xx_powerarc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_powerarc_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_powerarc_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_powerarc_path_safe(const char *name) {
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

static void xx_powerarc_stream_free(void *pointer) {
    xx_powerarc_stream *stream = (xx_powerarc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_powerarc_add(xx_powerarc_stream *stream,
                          const xx_powerarc_member *member) {
    xx_powerarc_member *grown = (xx_powerarc_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_POWERARC_MAGIC_SIZE 8
#define XX_POWERARC_HEADER_SIZE 8
#define XX_POWERARC_PROBE_SIZE 18
#define XX_POWERARC_MIN_STREAM 14
#define XX_POWERARC_MAX_MEMBERS 1
#define XX_POWERARC_METHOD_BZIP2 12U
#define XX_POWERARC_MAX_DECODED (256 * 1024 * 1024)
#define XX_POWERARC_MIN_CAPACITY (64 * 1024)
#define XX_POWERARC_GUESS_RATIO 4

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_powerarc_be32(const uint8_t *data);
static uint32_t xx_powerarc_be16(const uint8_t *data);
static bool xx_powerarc_block_magic_is_valid(const uint8_t *data);
static xx_powerarc_stream *xx_powerarc_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_powerarc_decode(Abstractformat *self, const xx_powerarc_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Header, then "BZh", the block-size digit and the 48-bit block magic. */
/* The shortest legal bzip2 stream: "BZh9", the 48-bit end-of-stream magic and
 * the 32-bit combined CRC. */
/* The container has no member table: one wrapper, one stream, one member. */
/* No method field exists; the magic itself says bzip2. The number is the one
 * a member listing should show, so it is the well-known bzip2 method id (12,
 * as in ZIP), not a library enum. */

static uint32_t xx_powerarc_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint32_t xx_powerarc_be16(const uint8_t *data) {
    return ((uint32_t)data[0] << 8) | (uint32_t)data[1];
}

/*
 * The 48-bit value that opens every bzip2 block is either the block magic
 * (0x314159265359, the digits of pi) or the end-of-stream magic
 * (0x177245385090, the digits of sqrt(pi)) for a stream with no blocks at
 * all. Nothing else can appear there.
 */
static bool xx_powerarc_block_magic_is_valid(const uint8_t *data) {
    uint32_t high = xx_powerarc_be32(data);
    uint32_t low = xx_powerarc_be16(data + 4);

    return (high == 0x31415926U && low == 0x5359U) ||
           (high == 0x17724538U && low == 0x5090U);
}

static xx_powerarc_stream *xx_powerarc_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    static const uint8_t expected[XX_POWERARC_MAGIC_SIZE] = {
        'B', 'Z', 'I', 'P', '0', '0', '0', '1'};
    xx_powerarc_stream *stream = NULL;
    xx_powerarc_member member;
    uint8_t probe[XX_POWERARC_PROBE_SIZE];
    int64_t total;
    int64_t span;
    int64_t payload_size;
    char *name;

    if (!self || !self->device || self->base_address < 0) return NULL;
    /* One member, so the cancellation check belongs here, before the single
     * iteration's worth of work. */
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The wrapper must be followed by a bzip2 stream that could conceivably
     * be complete; an 8-byte file carrying only the magic is not an archive. */
    if (span < XX_POWERARC_HEADER_SIZE + XX_POWERARC_MIN_STREAM) return NULL;
    if (!xx_powerarc_read_at(self, self->base_address, probe, sizeof(probe))) {
        return NULL;
    }
    /* The eight-byte magic is the primary signature. */
    if (xx_rt_memcmp(probe, expected, XX_POWERARC_MAGIC_SIZE) != 0) {
        return NULL;
    }
    /* ...and these three checks are the whole defence against a file that
     * merely starts with those eight letters: the payload has to actually be
     * a bzip2 stream. Loosen any of them and every "BZIP0001"-prefixed blob
     * in the world becomes a PowerArc archive whose extraction then fails. */
    if (probe[8] != 'B' || probe[9] != 'Z' || probe[10] != 'h') return NULL;
    if (probe[11] < '1' || probe[11] > '9') return NULL;
    if (!xx_powerarc_block_magic_is_valid(probe + 12)) return NULL;

    payload_size = span - XX_POWERARC_HEADER_SIZE;
    /* Trivially true given the floor above, but the member's extent is
     * checked against the span before it is published, like any other. */
    if (!xx_powerarc_range_within(span, XX_POWERARC_HEADER_SIZE,
                                  payload_size)) {
        return NULL;
    }

    stream = (xx_powerarc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* The container stores no name. The reference reader borrows the archive
     * file's own name minus ".pk"; a device has none here, so every PowerArc
     * member extracts as "data". */
    name = xx_str_dup("data");
    if (!name) goto fail;

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_POWERARC_HEADER_SIZE;
    member.data_offset = self->base_address + XX_POWERARC_HEADER_SIZE;
    member.compressed_size = payload_size;
    /* bzip2 records no original length and the wrapper adds none, so this is
     * genuinely unknown rather than zero; -1 is how the rest of the library
     * spells that. */
    member.uncompressed_size = -1;
    member.method = XX_POWERARC_METHOD_BZIP2;
    if (!xx_powerarc_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count > XX_POWERARC_MAX_MEMBERS) goto fail;

    stream->archive_size = span;
    return stream;

fail:
    xx_powerarc_stream_free(stream);
    return NULL;
}


/* A PowerArc payload is always bzip2 -- that is what the magic asserts -- so
 * there is exactly one method, and anything else must fail rather than fall
 * back to a stored copy, which would hand the caller compressed bytes dressed
 * up as content. */
/* Nothing in the container records the decoded length, so the first attempt
 * is a guess; these bound it. */

static bool xx_powerarc_decode(Abstractformat *self,
                               const xx_powerarc_member *member, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t packed_size;
    size_t capacity;
    size_t written = 0U;
    bool decoded = false;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member || !member->name) return false;
    if (member->method != XX_POWERARC_METHOD_BZIP2) return false;
    if (member->compressed_size <= 0 ||
        (uint64_t)member->compressed_size > (uint64_t)XX_POWERARC_MAX_DECODED) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    packed_size = (size_t)member->compressed_size;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    if (!xx_powerarc_read_at(self, member->data_offset, packed, packed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(packed);
        return false;
    }

    if (member->uncompressed_size >= 0) {
        /* Reached on every decode after xx_powerarc_measure() has run: the
         * measured length turns the growth loop into a single exact-size
         * attempt, so measuring costs one extra decode in total rather than
         * one per record. */
        if ((uint64_t)member->uncompressed_size >
            (uint64_t)XX_POWERARC_MAX_DECODED) {
            xx_mem_free(packed);
            return false;
        }
        capacity = (size_t)member->uncompressed_size;
    } else {
        capacity = packed_size;
        if (capacity <= (size_t)XX_POWERARC_MAX_DECODED /
                            (size_t)XX_POWERARC_GUESS_RATIO) {
            capacity *= (size_t)XX_POWERARC_GUESS_RATIO;
        } else {
            capacity = (size_t)XX_POWERARC_MAX_DECODED;
        }
        if (capacity < (size_t)XX_POWERARC_MIN_CAPACITY) {
            capacity = (size_t)XX_POWERARC_MIN_CAPACITY;
        }
    }

    /* The decoder treats a full output buffer as an error and never reports a
     * short write as success, so retrying with twice the room is safe: a
     * failure here is either a genuinely corrupt stream or a guess that was
     * too small, and the retry tells the two apart without ever publishing a
     * partial decode. */
    while (!decoded) {
        plain = (uint8_t *)xx_mem_alloc(capacity != 0U ? capacity : 1U);
        if (!plain) break;
        written = 0U;
        if (xx_bzip2_decompress_memory(packed, packed_size, plain, capacity,
                                       &written) &&
            written <= capacity) {
            decoded = true;
            break;
        }
        xx_mem_free(plain);
        plain = NULL;
        if (member->uncompressed_size >= 0) break; /* exact size, no retry */
        if (capacity >= (size_t)XX_POWERARC_MAX_DECODED) break;
        if (pd && xx_pd_is_stopped(pd)) break;
        capacity = (capacity > (size_t)XX_POWERARC_MAX_DECODED / 2U)
                       ? (size_t)XX_POWERARC_MAX_DECODED
                       : capacity * 2U;
    }
    xx_mem_free(packed);
    if (!decoded) {
        xx_mem_free(plain);
        return false;
    }
    /* When the container did claim a length, a decode that produced anything
     * else is a failure, not a partial success. */
    if (member->uncompressed_size >= 0 &&
        written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* bzip2 records no plaintext length and the PowerArc wrapper adds none, so
 * the only way to state one is to run the decode. That happens here, once per
 * member, and the answer is cached in uncompressed_size - which also makes
 * every later decode take the exact-size path above instead of repeating the
 * growth loop, so the 400 KiB fixture that forces a retry pays for it once.
 *
 * A member whose stream does not decode at all has no length to report, and
 * it is reported as zero rather than left unstated: "unstated" reads as
 * UINT64_MAX to a caller, and a caller that skips unknown-size members never
 * asks for the bytes, so the bzip2 CRC never gets to refuse them. The decode
 * path is untouched - a corrupt or truncated stream still fails there. */
static void xx_powerarc_measure(Abstractformat *self,
                                xx_powerarc_member *member) {
    uint8_t *plain = NULL;
    size_t plain_size = 0U;

    if (!member || member->uncompressed_size >= 0 || member->is_folder) return;
    if (xx_powerarc_decode(self, member, &plain, &plain_size, NULL)) {
        member->uncompressed_size = (int64_t)plain_size;
    } else {
        member->uncompressed_size = 0;
    }
    xx_mem_free(plain);
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_powerarc_init(xx_powerarc *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_POWERARC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-powerarc");
    xx_format_set_extension(&archive->format, "pk");
    archive->format.check_is_valid = xx_powerarc_check_is_valid;
    archive->format.handle_base_info = xx_powerarc_handle_base_info;
    archive->format.get_format_size = xx_powerarc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_powerarc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_powerarc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_powerarc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_powerarc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_powerarc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_powerarc_free_archive_records_reading;
    archive->format.destroy = xx_powerarc_vtable_destroy;
}

xx_powerarc *xx_powerarc_create(xx_io_device *device, int64_t base_address) {
    xx_powerarc *archive = (xx_powerarc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_powerarc_init(archive, device, base_address);
    return archive;
}

void xx_powerarc_destroy(xx_powerarc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_powerarc_free(xx_powerarc *archive) {
    if (!archive) return;
    xx_powerarc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_powerarc_vtable_destroy(Abstractformat *self) {
    xx_powerarc_destroy((xx_powerarc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_powerarc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_powerarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_powerarc_parse(self, pd);
    if (!stream) return false;
    xx_powerarc_stream_free(stream);
    return true;
}

bool xx_powerarc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_powerarc *archive = (xx_powerarc *)self;
    xx_powerarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_powerarc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_powerarc_stream_free(stream);
    return true;
}

int64_t xx_powerarc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_powerarc_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_powerarc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_powerarc_set_record(Abstractformat *self,
                                 xx_archive_record *record,
                                 xx_powerarc_member *member) {
    xx_powerarc_measure(self, member);
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

static bool xx_powerarc_copy_options(xx_list_s *target,
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

static const xx_var *xx_powerarc_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_powerarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_powerarc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_powerarc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_powerarc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_powerarc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_powerarc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_powerarc_set_record(self, &state->current_record,
                                 &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_powerarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_powerarc_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_powerarc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_powerarc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_powerarc_set_record(self, &state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_powerarc_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_powerarc_stream *stream;
    const xx_powerarc_member *member;
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
    stream = (xx_powerarc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_powerarc_path_safe(member->name)) return false;

    path_option = xx_powerarc_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_powerarc_decode(self, member, &plain, &plain_size, pd);
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
        !xx_powerarc_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_powerarc_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
