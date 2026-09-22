/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * NewWave LZWD packed files, the single-file compressor of the HP NewWave
 * desktop. This is a wrapper around ONE payload, not a multi-member archive,
 * so the record list this reader publishes always holds exactly one entry.
 *
 *   header, 11 bytes at offset 0:
 *     0x00   4  u32 LE   plaintext length, read by the reference as a SIGNED
 *                        32-bit value and rejected when negative
 *     0x04   4  u32 LE   magic 0x575A4CFC (bytes FC 4C 5A 57)
 *     0x08   2  u16 LE   version, always 0x0001
 *     0x0a   1  u8       flags, always 0x00
 *     0x0b   n  payload  LZW code stream, running to end-of-file
 *
 * The first payload byte is part of detection, not of the header: the stream
 * opens with a nine-bit MSB-first CLEAR (0x100), whose top eight bits are
 * exactly 0x80, so a valid container always has 0x80 at offset 0x0b. The
 * reference's search signature is "........FC4C5A5701000080" for that
 * reason.
 *
 * THE ORIGINAL FILE NAME IS STORED NOWHERE. The reference reader borrows the
 * container's own file name; this reader has no access to that, so it
 * publishes a fixed placeholder instead.
 *
 * The codec looks like TIFF/PDF LZW - MSB-first, 9..12 bits, CLEAR 0x100,
 * END 0x101, first free code 0x102, early width change - but differs in one
 * rule that matters: at a full 4096-entry table it SELF-RESTARTS rather than
 * stalling and waiting for an explicit CLEAR. The CLEAR the encoder then
 * writes is read at nine bits and is a no-op. A decoder that stalls
 * desynchronises immediately, which is why this needs its own entry point
 * rather than the Aldus/PDF one.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lzwd/xx_lzwd.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzwvariants/xx_lzwvariants.h"

#include <stdio.h>

#define XX_LZWD_COPY_CHUNK (64 * 1024)

typedef struct xx_lzwd_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_lzwd_member;

typedef struct xx_lzwd_stream_s {
    xx_lzwd_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_lzwd_stream;

static void xx_lzwd_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_lzwd_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_lzwd_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_lzwd_path_safe(const char *name) {
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

static void xx_lzwd_stream_free(void *pointer) {
    xx_lzwd_stream *stream = (xx_lzwd_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_lzwd_add(xx_lzwd_stream *stream,
                          const xx_lzwd_member *member) {
    xx_lzwd_member *grown = (xx_lzwd_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_LZWD_MIN_PACKED_SIZE 2
#define XX_LZWD_MAX_MEMBERS 1
#define XX_LZWD_MAX_RATIO 4096
#define XX_LZWD_RATIO_SLACK 8192
#define XX_LZWD_PLACEHOLDER_NAME "lzwd_data"
#define XX_LZWD_HEADER_SIZE 11
#define XX_LZWD_PROBE_SIZE 12
#define XX_LZWD_MAGIC 0x575A4CFCU
#define XX_LZWD_MAGIC_OFFSET 4
#define XX_LZWD_VERSION_OFFSET 8
#define XX_LZWD_FLAGS_OFFSET 10
#define XX_LZWD_VERSION 0x0001U
#define XX_LZWD_FLAGS 0x00U
#define XX_LZWD_FIRST_STREAM_BYTE 0x80U
#define XX_LZWD_METHOD_LZW 1U
#define XX_LZWD_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_lzwd_le16(const uint8_t *data);
static uint32_t xx_lzwd_le32(const uint8_t *data);
static xx_lzwd_stream *xx_lzwd_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_lzwd_decode(Abstractformat *self, const xx_lzwd_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The opening CLEAR alone spans nine bits, so a stream is never shorter than
 * two bytes. */
/* This container wraps exactly one payload. The cap exists because the
 * contract asks for one, not because a chain could ever grow. */
/* Decompression-bomb guard. A twelve-bit table tops out around four thousand
 * bytes per phrase, so 4096:1 leaves headroom while still refusing a header
 * claiming a gigabyte behind a dozen bytes of payload. */
/* The original name is stored nowhere and this reader cannot see the
 * container's own file name, so the single record gets a fixed placeholder.
 * It is deliberately extension-less: inventing one would be a claim about
 * content the container never makes. */

static uint16_t xx_lzwd_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_lzwd_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_lzwd_stream *xx_lzwd_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzwd_stream *stream;
    xx_lzwd_member member;
    uint8_t probe[XX_LZWD_PROBE_SIZE];
    char *name;
    int64_t total;
    int64_t span;
    int64_t compressed_size;
    uint32_t raw_size;
    int64_t uncompressed_size;

    if (!self || !self->device || self->base_address < 0) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Strictly more than the probe: a container whose payload is only the
     * opening CLEAR byte carries no data. */
    if (span <= XX_LZWD_PROBE_SIZE) return NULL;
    if (!xx_lzwd_read_at(self, self->base_address, probe, sizeof(probe))) {
        return NULL;
    }

    /* THE FORMAT'S GATE, and the reason it is a strong one: four magic bytes
     * at 0x04, a pinned version word, a pinned flags byte and the fixed
     * first payload byte add up to 56 bits that must all agree. The last of
     * them, the 0x80, is the one a later reader will be tempted to drop as
     * "part of the data" - it is not: it is the top eight bits of the
     * nine-bit CLEAR that opens every stream, and dropping it costs a
     * quarter of the discrimination. */
    if (xx_lzwd_le32(probe + XX_LZWD_MAGIC_OFFSET) != XX_LZWD_MAGIC) {
        return NULL;
    }
    if (xx_lzwd_le16(probe + XX_LZWD_VERSION_OFFSET) !=
        (uint16_t)XX_LZWD_VERSION) {
        return NULL;
    }
    if (probe[XX_LZWD_FLAGS_OFFSET] != (uint8_t)XX_LZWD_FLAGS) return NULL;
    if (probe[XX_LZWD_HEADER_SIZE] != (uint8_t)XX_LZWD_FIRST_STREAM_BYTE) {
        return NULL;
    }

    raw_size = xx_lzwd_le32(probe);
    /* The reference reads the length as a signed int32 and refuses a
     * negative one, so the top bit being set is a rejection rather than a
     * two-gigabyte member. */
    if ((raw_size & 0x80000000U) != 0U) return NULL;
    uncompressed_size = (int64_t)raw_size;
    /* A zero plaintext length would make extraction write an empty file and
     * call it success, so it is a reject rather than an empty member. */
    if (uncompressed_size < 1) return NULL;
    if (uncompressed_size > XX_LZWD_MAX_DECODED) return NULL;

    /* The payload is everything behind the header: the container stores no
     * packed size, so end-of-file is the only boundary there is. Note the
     * payload starts at 0x0b, NOT at the end of the twelve-byte probe - the
     * twelfth byte is the stream's first byte and belongs to the member. */
    compressed_size = span - XX_LZWD_HEADER_SIZE;
    if (compressed_size < XX_LZWD_MIN_PACKED_SIZE) return NULL;
    if (compressed_size > XX_LZWD_MAX_DECODED) return NULL;
    if (uncompressed_size >
        (compressed_size * XX_LZWD_MAX_RATIO) + XX_LZWD_RATIO_SLACK) {
        return NULL;
    }
    if (!xx_lzwd_range_within(span, (int64_t)XX_LZWD_HEADER_SIZE,
                              compressed_size)) {
        return NULL;
    }

    stream = (xx_lzwd_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(XX_LZWD_PLACEHOLDER_NAME);
    if (!name) goto fail;
    if (!xx_lzwd_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = XX_LZWD_HEADER_SIZE;
    member.data_offset = self->base_address + XX_LZWD_HEADER_SIZE;
    member.compressed_size = compressed_size;
    member.uncompressed_size = uncompressed_size;
    member.method = XX_LZWD_METHOD_LZW;
    /* The header has no date or time field anywhere; reporting anything but
     * zero here would be an invention. */
    member.timestamp = 0U;
    /* The wrapper has no directory entries and never will: it holds one
     * file. */
    member.is_folder = false;

    if (!xx_lzwd_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    if (stream->count != (size_t)XX_LZWD_MAX_MEMBERS) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_lzwd_stream_free(stream);
    return NULL;
}


/* The header plus the first stream byte: the reference probes twelve bytes
 * because the twelfth is a detection field even though it belongs to the
 * payload. */
/* Top eight bits of the nine-bit MSB-first CLEAR code that opens every
 * stream. */

/* The container carries no method field - the magic is the codec - so parse
 * stamps this one synthetic value and decode refuses anything else. Treating
 * an unrecognised value as stored would hand the caller garbage that looks
 * exactly like data. */

/* The plaintext length is attacker-controlled, so the allocation it drives
 * is capped. */

/* The single payload is a complete NewWave LZW stream. Unlike the K-BOOM
 * dialect the stream does carry an END code, but the header's length is
 * still the success test: the reference keeps an overrunning final phrase
 * and then fails its exact-size comparison, and the codec does the same. */
static bool xx_lzwd_decode(Abstractformat *self, const xx_lzwd_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->method != XX_LZWD_METHOD_LZW) return false;
    if (member->compressed_size < 1 || member->uncompressed_size < 1) {
        return false;
    }
    if (member->compressed_size > XX_LZWD_MAX_DECODED ||
        member->uncompressed_size > XX_LZWD_MAX_DECODED) {
        return false;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_lzwd_read_at(self, member->data_offset, input,
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
    /* Exactly the declared plaintext length, or nothing. The format carries
     * no checksum, so this equality is the whole of extraction's correctness
     * check, and a short decode reported as success is the one failure the
     * caller cannot detect. */
    if (!xx_lzwvariants_newwave_decode_memory(input,
                                              (size_t)member->compressed_size,
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

void xx_lzwd_init(xx_lzwd *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_LZWD;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-newwave-lzw");
    xx_format_set_extension(&archive->format, "lzw");
    archive->format.check_is_valid = xx_lzwd_check_is_valid;
    archive->format.handle_base_info = xx_lzwd_handle_base_info;
    archive->format.get_format_size = xx_lzwd_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lzwd_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lzwd_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lzwd_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lzwd_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lzwd_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lzwd_free_archive_records_reading;
    archive->format.destroy = xx_lzwd_vtable_destroy;
}

xx_lzwd *xx_lzwd_create(xx_io_device *device, int64_t base_address) {
    xx_lzwd *archive = (xx_lzwd *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lzwd_init(archive, device, base_address);
    return archive;
}

void xx_lzwd_destroy(xx_lzwd *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lzwd_free(xx_lzwd *archive) {
    if (!archive) return;
    xx_lzwd_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lzwd_vtable_destroy(Abstractformat *self) {
    xx_lzwd_destroy((xx_lzwd *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lzwd_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzwd_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lzwd_parse(self, pd);
    if (!stream) return false;
    xx_lzwd_stream_free(stream);
    return true;
}

bool xx_lzwd_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_lzwd *archive = (xx_lzwd *)self;
    xx_lzwd_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lzwd_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_lzwd_stream_free(stream);
    return true;
}

int64_t xx_lzwd_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lzwd_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lzwd *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lzwd_set_record(xx_archive_record *record,
                                 const xx_lzwd_member *member) {
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

static bool xx_lzwd_copy_options(xx_list_s *target,
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

static const xx_var *xx_lzwd_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_lzwd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_lzwd_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lzwd_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lzwd_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lzwd_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lzwd_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_lzwd_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lzwd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_lzwd_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_lzwd_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lzwd_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_lzwd_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lzwd_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_lzwd_stream *stream;
    const xx_lzwd_member *member;
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
    stream = (xx_lzwd_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lzwd_path_safe(member->name)) return false;

    path_option = xx_lzwd_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_lzwd_decode(self, member, &plain, &plain_size, pd);
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
        !xx_lzwd_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_lzwd_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
