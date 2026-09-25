/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Electronic Arts RefPack / QFS containers (.qfs, .fsh, .cfs, .ori, .iff).
 *
 * A whole-file compressor, not a multi-member archive: one payload, no
 * directory, no name. The header is a two-byte signature word followed by
 * one or two BIG-endian size fields.
 *
 *   0x00  u8       flags; bit 0x10 must be set and bits 0x02..0x40 clear
 *   0x01  u8       the constant 0xFB
 *   0x02  u8[3|4]  packed size, present only when flags & 0x80; read and
 *                  skipped -- the command grammar is what delimits the
 *                  stream
 *   +     u8[3|4]  uncompressed size
 *
 * Both size fields are three bytes wide, or four when flags & 0x01. The
 * command stream begins immediately behind the header; there is no alignment
 * padding and no table.
 *
 * Command grammar, keyed on the first byte:
 *
 *   0x00-0x7F  2 bytes  literals = b0 & 3
 *                       copy     = ((b0 & 0x1C) >> 2) + 3
 *                       distance = ((b0 & 0x60) << 3) + b1 + 1
 *   0x80-0xBF  3 bytes  literals = (b1 >> 6) & 3
 *                       copy     = (b0 & 0x3F) + 4
 *                       distance = ((b1 & 0x3F) << 8) + b2 + 1
 *   0xC0-0xDF  4 bytes  literals = b0 & 3
 *                       copy     = ((b0 & 0x0C) << 6) + b3 + 5
 *                       distance = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
 *   0xE0-0xFB  1 byte   literals = ((b0 & 0x1F) << 2) + 4, no copy
 *   0xFC-0xFF  1 byte   literals = b0 & 3, terminator
 *
 * RefPack has no pre-filled window, so a back reference may never point in
 * front of the output produced so far. That, together with the required
 * terminator, is what makes the grammar usable as a validity probe -- which
 * it has to be, because a two-byte signature matches roughly one file in
 * 8000 by chance.
 *
 * The container names nothing, so the single member is published under a
 * fixed name; the reference extractor derives one from the host file name
 * and falls back to the same thing.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/earefpack/xx_earefpack.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/ea/xx_ea.h"

#include <stdio.h>

#define XX_EAREFPACK_COPY_CHUNK (64 * 1024)

typedef struct xx_earefpack_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_earefpack_member;

typedef struct xx_earefpack_stream_s {
    xx_earefpack_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_earefpack_stream;

static void xx_earefpack_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_earefpack_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_earefpack_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_earefpack_path_safe(const char *name) {
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

static void xx_earefpack_stream_free(void *pointer) {
    xx_earefpack_stream *stream = (xx_earefpack_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_earefpack_add(xx_earefpack_stream *stream,
                          const xx_earefpack_member *member) {
    xx_earefpack_member *grown = (xx_earefpack_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_EAREFPACK_MIN_SIZE 6
#define XX_EAREFPACK_MAX_INPUT (64 * 1024 * 1024)
#define XX_EAREFPACK_MAX_MEMBERS 1
#define XX_EAREFPACK_MAGIC_LOW 0xfbU
#define XX_EAREFPACK_FLAG_BASE 0x10U
#define XX_EAREFPACK_FLAG_LARGE 0x01U
#define XX_EAREFPACK_FLAG_PACKED 0x80U
#define XX_EAREFPACK_FLAG_MASK 0x7eU
#define XX_EAREFPACK_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static xx_earefpack_stream *xx_earefpack_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_earefpack_decode(Abstractformat *self, const xx_earefpack_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Signature word plus the narrowest size field: nothing shorter can carry a
 * header, let alone a terminator command. */
/* The library's scan walks the grammar to its terminator, so it needs the
 * whole container in memory; there is no prefix mode. A container above this
 * is refused rather than probed, because the alternative is a buffer of
 * arbitrary size allocated on the strength of a two-byte signature. */
/* One payload, no directory. */
/* Bits 0x02..0x40 are unassigned. Requiring them clear is the cheap half of
 * the detection: it is what keeps a stray 0xFB byte in ordinary data from
 * being read as a header at all. */

static xx_earefpack_stream *xx_earefpack_parse(Abstractformat *self,
                                               xx_pd_struct *pd) {
    xx_earefpack_stream *stream = NULL;
    xx_earefpack_member member;
    uint8_t signature[2];
    uint8_t *container = NULL;
    size_t consumed = 0U;
    size_t produced = 0U;
    int64_t total;
    int64_t span;
    int64_t header_size;
    int64_t field;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_EAREFPACK_MIN_SIZE) return NULL;
    if (span > XX_EAREFPACK_MAX_INPUT) return NULL;

    if (!xx_earefpack_read_at(self, self->base_address, signature, 2U)) {
        return NULL;
    }
    if (signature[1] != XX_EAREFPACK_MAGIC_LOW) return NULL;
    if ((signature[0] & XX_EAREFPACK_FLAG_MASK) != XX_EAREFPACK_FLAG_BASE) {
        return NULL;
    }

    /* Header width is derived from the flags, so it is known before the
     * stream is walked; the scan re-derives it and the two must agree by
     * construction. */
    field = (signature[0] & XX_EAREFPACK_FLAG_LARGE) ? 4 : 3;
    header_size = 2 + field;
    if (signature[0] & XX_EAREFPACK_FLAG_PACKED) header_size += field;
    /* An empty command stream cannot exist: the shortest one is still a
     * terminator byte. */
    if (!xx_earefpack_range_within(span, header_size, 1)) return NULL;

    if (pd && xx_pd_is_stopped(pd)) return NULL;

    container = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!container) return NULL;
    if (!xx_earefpack_read_at(self, self->base_address, container,
                              (size_t)span)) {
        xx_mem_free(container);
        return NULL;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(container);
        return NULL;
    }

    /* The two-byte signature is far too weak to accept on, so acceptance
     * rests entirely on the grammar: the scan succeeds only when every
     * command is well formed, no back reference points in front of the
     * output, the stream closes on an explicit 0xFC..0xFF terminator, and
     * the bytes it produced equal the size declared in the header. It
     * materialises nothing -- it only counts. */
    if (!xx_ea_refpack_scan_memory(container, (size_t)span,
                                   (size_t)XX_EAREFPACK_MAX_DECODED,
                                   &consumed, &produced)) {
        xx_mem_free(container);
        return NULL;
    }
    xx_mem_free(container);
    container = NULL;
    /* The stream must be the whole file: every corpus sample ends on its
     * last byte. Allowing a short stream with a trailing remainder would
     * turn the grammar probe from a decision into a guess, because a random
     * file can satisfy a few commands and then stop. */
    if ((int64_t)consumed != span) return NULL;
    if (produced == 0U) return NULL;

    stream = (xx_earefpack_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    xx_mem_zero(&member, sizeof(member));
    /* Nothing in the container names the payload; the reference derives the
     * name from the host file and falls back to this, which is all that is
     * available here. */
    member.name = xx_str_dup("refpack.bin");
    if (!member.name) goto fail;
    member.header_offset = self->base_address;
    member.header_size = header_size;
    /* The stream deliberately starts AT the signature word rather than
     * behind the header: the decoder is handed the whole container and
     * re-reads the size fields itself, so handing it a headerless slice
     * would fail. */
    member.data_offset = self->base_address;
    member.compressed_size = span;
    member.uncompressed_size = (int64_t)produced;
    /* The container's own flags byte, unchanged: there is no method field,
     * and the flags are the only thing that distinguishes one container form
     * from another. */
    member.method = (uint32_t)signature[0];
    member.timestamp = 0U;
    member.is_folder = false;
    if (!xx_earefpack_add(stream, &member)) {
        xx_str_free(member.name);
        goto fail;
    }

    stream->archive_size = span;
    return stream;

fail:
    xx_earefpack_stream_free(stream);
    return NULL;
}


/* The uncompressed size is a header field of an attacker-supplied container,
 * so it is capped before it becomes an allocation. The parse passes the same
 * ceiling to the scan, which keeps detection and extraction from disagreeing
 * about which containers exist. */

static bool xx_earefpack_decode(Abstractformat *self,
                                const xx_earefpack_member *member,
                                uint8_t **out, size_t *out_size,
                                xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t plain_size;
    size_t written = 0U;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* There is no method field: method carries the container's flags byte.
     * Anything that is not a RefPack flags word means the member did not
     * come from this parse, and decoding it anyway would run the grammar on
     * unrelated bytes. */
    if ((member->method & XX_EAREFPACK_FLAG_MASK) !=
        XX_EAREFPACK_FLAG_BASE) {
        return false;
    }
    /* Neither an empty container nor an empty payload is representable: the
     * shortest stream is a header plus a terminator command, and a
     * terminator still produces at least the literals it carries. */
    if (member->compressed_size < XX_EAREFPACK_MIN_SIZE) return false;
    if (member->uncompressed_size <= 0) return false;
    if (member->uncompressed_size > XX_EAREFPACK_MAX_DECODED) return false;
    if (member->compressed_size > XX_EAREFPACK_MAX_INPUT) return false;

    plain_size = (size_t)member->uncompressed_size;
    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    /* data_offset is the signature word, not the first command: the decoder
     * parses the header itself. */
    if (!xx_earefpack_read_at(self, member->data_offset, packed,
                              (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    /* The output capacity is the size the parse measured. The decoder
     * refuses outright when the header declares more than fits, so a
     * container whose header disagrees with the scan cannot slip through. */
    if (!xx_ea_refpack_decode_memory(packed, (size_t)member->compressed_size,
                                     plain, plain_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* Returning true with fewer bytes than the parse measured is the one
     * failure the caller cannot detect. The decoder already demands the
     * terminator and the declared length, so this is the belt on top of the
     * braces -- and the thing a later reader will be tempted to drop. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_earefpack_init(xx_earefpack *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_EAREFPACK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ea-refpack");
    xx_format_set_extension(&archive->format, "qfs");
    archive->format.check_is_valid = xx_earefpack_check_is_valid;
    archive->format.handle_base_info = xx_earefpack_handle_base_info;
    archive->format.get_format_size = xx_earefpack_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_earefpack_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_earefpack_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_earefpack_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_earefpack_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_earefpack_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_earefpack_free_archive_records_reading;
    archive->format.destroy = xx_earefpack_vtable_destroy;
}

xx_earefpack *xx_earefpack_create(xx_io_device *device, int64_t base_address) {
    xx_earefpack *archive = (xx_earefpack *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_earefpack_init(archive, device, base_address);
    return archive;
}

void xx_earefpack_destroy(xx_earefpack *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_earefpack_free(xx_earefpack *archive) {
    if (!archive) return;
    xx_earefpack_destroy(archive);
    xx_mem_free(archive);
}

static void xx_earefpack_vtable_destroy(Abstractformat *self) {
    xx_earefpack_destroy((xx_earefpack *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_earefpack_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_earefpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_earefpack_parse(self, pd);
    if (!stream) return false;
    xx_earefpack_stream_free(stream);
    return true;
}

bool xx_earefpack_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_earefpack *archive = (xx_earefpack *)self;
    xx_earefpack_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_earefpack_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_earefpack_stream_free(stream);
    return true;
}

int64_t xx_earefpack_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_earefpack_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_earefpack *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_earefpack_set_record(xx_archive_record *record,
                                 const xx_earefpack_member *member) {
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

static bool xx_earefpack_copy_options(xx_list_s *target,
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

static const xx_var *xx_earefpack_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_earefpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_earefpack_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_earefpack_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_earefpack_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_earefpack_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_earefpack_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_earefpack_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_earefpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_earefpack_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_earefpack_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_earefpack_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_earefpack_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_earefpack_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_earefpack_stream *stream;
    const xx_earefpack_member *member;
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
    stream = (xx_earefpack_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_earefpack_path_safe(member->name)) return false;

    path_option = xx_earefpack_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_earefpack_decode(self, member, &plain, &plain_size, pd);
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
        !xx_earefpack_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_earefpack_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
