/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ZIE - "ProtectIt/2" (OS/2) encrypted ZIP wrappers.
 *
 *   0x000  u32 LE 0x32544950 = "PIT2"
 *   0x004  16 bytes  obfuscated key
 *   0x014  13 bytes  original file name, NUL padded, 8.3-ish
 *   0x024 .. 0x118   zero padding
 *   0x118  the encrypted payload, running to end-of-file
 *
 * THERE IS NO COMPRESSION. The payload decrypts to exactly its own length -
 * an ordinary ZIP - so the member's compressed and uncompressed sizes are
 * the same number and the container needs no size field at all.
 *
 * THE KEY is the blob at +4 XORed with the product name, "ProtectIt/2 OS/2".
 *
 * THE CIPHER is a 16-byte repeating XOR whose PHASE COMES FROM THE PAYLOAD
 * LENGTH: the length rounded down to a multiple of four decides the rotation,
 * with a further +8 over the first 0xC0000 bytes of a large payload, and the
 * last (payload % 4) bytes are left in the clear.
 *
 * That makes the phase WRONG FOR A TRUNCATED FILE, whose length the encryptor
 * never saw and whose true phase is 0. xx_zie_resolve_method() therefore
 * resolves the phase by known plaintext - the archive underneath must begin
 * with "PK" 03 04 - trying the length-derived phase first and falling back to
 * phase 0. On intact files that reproduces the reference exactly; on
 * truncated ones it recovers archives the reference extracts nothing from.
 *
 * THAT RESOLUTION IS ALSO THE DETECTOR. Four bytes of magic and a name field
 * are weak; requiring that the first four payload bytes decrypt to a local
 * file header signature under one of exactly two candidate phases is what
 * makes a false positive essentially impossible.
 *
 * The container carries one member, no member table, no timestamp and no
 * CRC of its own - the CRCs are inside the ZIP.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/zie/xx_zie.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/zie/xx_zie.h"

#include <stdio.h>

#define XX_ZIE_COPY_CHUNK (64 * 1024)

typedef struct xx_zie_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_zie_member;

typedef struct xx_zie_stream_s {
    xx_zie_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_zie_stream;

static void xx_zie_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_zie_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_zie_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_zie_path_safe(const char *name) {
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

static void xx_zie_stream_free(void *pointer) {
    xx_zie_stream *stream = (xx_zie_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_zie_add(xx_zie_stream *stream,
                          const xx_zie_member *member) {
    xx_zie_member *grown = (xx_zie_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_ZIE_MIN_SIZE ((int64_t)XX_ZIE_HEADER_SIZE + 4)
#define XX_ZIE_MAX_MEMBERS 1
#define XX_ZIE_METHOD_XOR16 0U
#define XX_ZIE_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_ZIE_NAME_BUFFER 16
#define XX_ZIE_FALLBACK_NAME "archive.zip"

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static xx_zie_stream *xx_zie_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_zie_decode(Abstractformat *self, const xx_zie_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The header plus the four payload bytes the phase is resolved against. */
/* Exactly one member; the cap exists only for shape. */

/* The container has no method field at all - see the decode. */

/* The payload is decrypted as one buffer, so this caps the allocation. The
 * same limit is applied in parse so a container this reader cannot extract is
 * never listed as one it can. The reference has no such ceiling; a ZIE over
 * 256 MiB is refused here rather than attempted. */

/* xx_zie_file_name() needs 14 bytes; this is that, rounded up. */

/* Used only when the header's name field is empty. The payload is always a
 * ZIP, so the extension is not a guess. */

static xx_zie_stream *xx_zie_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_zie_stream *stream;
    xx_zie_member member;
    xx_zie_method method;
    uint8_t header[XX_ZIE_HEADER_SIZE];
    uint8_t probe[4];
    char buffer[XX_ZIE_NAME_BUFFER];
    char *name;
    int64_t total;
    int64_t span;
    int64_t payload_size;
    size_t length = 0U;
    size_t index;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A header with fewer than four payload bytes cannot have its phase
     * resolved, and an empty payload is not a ZIP. */
    if (span < XX_ZIE_MIN_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_zie_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }

    /* Magic plus the reference's own 8.3-ish check on the name field at
     * +0x14. Four bytes of "PIT2" alone would match too often, and the name
     * check is the cheap half of the defence - but only the cheap half. */
    if (!xx_zie_is_valid_header(header, sizeof(header))) return NULL;

    payload_size = span - (int64_t)XX_ZIE_HEADER_SIZE;
    if (!xx_zie_range_within(span, (int64_t)XX_ZIE_HEADER_SIZE,
                             payload_size)) {
        return NULL;
    }
    /* See XX_ZIE_MAX_DECODED: refusing here keeps the listing and the
     * extraction in agreement. */
    if (payload_size > XX_ZIE_MAX_DECODED) return NULL;

    if (!xx_zie_read_at(self, self->base_address + (int64_t)XX_ZIE_HEADER_SIZE,
                        probe, sizeof(probe))) {
        return NULL;
    }
    xx_mem_zero(&method, sizeof(method));
    /* THE REAL DETECTOR. The phase is a function of the payload length, so
     * resolving it and finding "PK" 03 04 underneath means the key derived
     * from this header decrypts this payload into a ZIP. A file that passes
     * the magic and the name check but fails here is not a ZIE, and this is
     * the check that must never be loosened into "resolve, and if it fails
     * assume phase 0 anyway" - phase 0 is already one of the two candidates
     * the resolver tries. */
    if (!xx_zie_resolve_method(header, sizeof(header), probe, sizeof(probe),
                               (uint64_t)payload_size, &method)) {
        return NULL;
    }

    /* The stored name is the original file's, NUL padded inside a 13-byte
     * field; is_valid_header has already vetted its character set. */
    if (!xx_zie_file_name(header, sizeof(header), buffer, sizeof(buffer),
                          &length)) {
        return NULL;
    }
    for (index = 0U; index < length; ++index) {
        /* Belt and braces over the reference's own check: a name byte that
         * is not printable ASCII would become a path component. */
        if ((uint8_t)buffer[index] < 0x20U ||
            (uint8_t)buffer[index] > 0x7eU) {
            return NULL;
        }
    }

    stream = (xx_zie_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    name = xx_str_dup(length != 0U ? buffer : XX_ZIE_FALLBACK_NAME);
    if (!name) goto fail;
    if (!xx_zie_path_safe(name)) {
        xx_str_free(name);
        goto fail;
    }

    xx_mem_zero(&member, sizeof(member));
    member.name = name;
    member.header_offset = self->base_address;
    member.header_size = (int64_t)XX_ZIE_HEADER_SIZE;
    member.data_offset = self->base_address + (int64_t)XX_ZIE_HEADER_SIZE;
    /* No compression: the payload decrypts to exactly its own length. */
    member.compressed_size = payload_size;
    member.uncompressed_size = payload_size;
    member.method = XX_ZIE_METHOD_XOR16;
    /* The wrapper carries no timestamp; the ZIP inside carries its own. */
    member.timestamp = 0U;
    member.is_folder = false;

    if (!xx_zie_add(stream, &member)) {
        xx_str_free(name);
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_zie_stream_free(stream);
    return NULL;
}


static bool xx_zie_decode(Abstractformat *self, const xx_zie_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t header[XX_ZIE_HEADER_SIZE];
    xx_zie_method method;
    uint8_t *payload;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    /* The container has no method field: the 16-byte XOR is the only thing
     * it does. parse stamps this one synthetic value and anything else means
     * the member did not come from this parse. */
    if (member->method != XX_ZIE_METHOD_XOR16) return false;
    /* No compression, so the two sizes must be the same number; a member
     * where they differ was tampered with. */
    if (member->compressed_size < 4 ||
        member->compressed_size != member->uncompressed_size ||
        member->compressed_size > XX_ZIE_MAX_DECODED) {
        return false;
    }

    /* The phase is resolved again rather than carried in the member: it is
     * derived from the header and the first payload bytes, not stored, so
     * re-deriving it is what makes an extraction that runs against a changed
     * file fail instead of decrypting with a stale key. */
    if (!xx_zie_read_at(self, member->header_offset, header,
                        sizeof(header))) {
        return false;
    }

    payload = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!payload) return false;
    if (!xx_zie_read_at(self, member->data_offset, payload,
                        (size_t)member->compressed_size)) {
        xx_mem_free(payload);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(payload);
        return false;
    }

    xx_mem_zero(&method, sizeof(method));
    if (!xx_zie_resolve_method(header, sizeof(header), payload, 4U,
                               (uint64_t)member->compressed_size, &method)) {
        xx_mem_free(payload);
        return false;
    }
    /* In place: the cipher is a byte-for-byte XOR, so the payload buffer is
     * both source and destination and the header's guarantee about full
     * overlap covers it. */
    if (!xx_zie_decode_method(payload, (size_t)member->compressed_size,
                              &method, payload,
                              (size_t)member->compressed_size, &written) ||
        written != (size_t)member->compressed_size) {
        xx_mem_free(payload);
        return false;
    }
    *out = payload;
    *out_size = (size_t)member->compressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_zie_init(xx_zie *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_ZIE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-zie");
    xx_format_set_extension(&archive->format, "zie");
    archive->format.check_is_valid = xx_zie_check_is_valid;
    archive->format.handle_base_info = xx_zie_handle_base_info;
    archive->format.get_format_size = xx_zie_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_zie_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_zie_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_zie_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_zie_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_zie_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_zie_free_archive_records_reading;
    archive->format.destroy = xx_zie_vtable_destroy;
}

xx_zie *xx_zie_create(xx_io_device *device, int64_t base_address) {
    xx_zie *archive = (xx_zie *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_zie_init(archive, device, base_address);
    return archive;
}

void xx_zie_destroy(xx_zie *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_zie_free(xx_zie *archive) {
    if (!archive) return;
    xx_zie_destroy(archive);
    xx_mem_free(archive);
}

static void xx_zie_vtable_destroy(Abstractformat *self) {
    xx_zie_destroy((xx_zie *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_zie_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_zie_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_zie_parse(self, pd);
    if (!stream) return false;
    xx_zie_stream_free(stream);
    return true;
}

bool xx_zie_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_zie *archive = (xx_zie *)self;
    xx_zie_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_zie_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_zie_stream_free(stream);
    return true;
}

int64_t xx_zie_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_zie_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_zie *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_zie_set_record(xx_archive_record *record,
                                 const xx_zie_member *member) {
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

static bool xx_zie_copy_options(xx_list_s *target,
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

static const xx_var *xx_zie_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_zie_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_zie_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_zie_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_zie_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_zie_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_zie_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_zie_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_zie_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_zie_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_zie_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_zie_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_zie_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_zie_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_zie_stream *stream;
    const xx_zie_member *member;
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
    stream = (xx_zie_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_zie_path_safe(member->name)) return false;

    path_option = xx_zie_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_zie_decode(self, member, &plain, &plain_size, pd);
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
        !xx_zie_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_zie_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
