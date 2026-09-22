/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Maximus FIZ archives.
 *
 * There is no container header and no central directory: the file IS the
 * member chain, starting at offset 0.
 *
 *   member header, 20 bytes, 4+1+1+2+4+4+2+2 with no slack:
 *     0x00  4 bytes magic, "FIZ" 0x1a
 *     0x04  u8  method: 0 = stored, 1 = LHA -lh5-
 *     0x05  u8  name length, 1..12
 *     0x06  u16 LE CRC-16/ARC over the UNPACKED member
 *     0x08  u32 LE uncompressed size
 *     0x0c  u32 LE compressed size
 *     0x10  u16 LE DOS time
 *     0x12  u16 LE DOS date
 *     0x14  name, exactly nameLen bytes, NOT NUL terminated
 *           payload, exactly compressedSize bytes
 *
 * The magic sits on every member, not only the first, which is what makes a
 * truncated or spliced chain fail closed. The chain has no terminator record:
 * it ends by landing exactly on end-of-file. A short tail is a rejection, not
 * an overlay - no FIZ archive has slack, and accepting one would turn any
 * four-byte prefix match into a hit.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fiz/xx_fiz.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzh/xx_lzh.h"

#include <stdio.h>

#define XX_FIZ_COPY_CHUNK (64 * 1024)

typedef struct xx_fiz_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_fiz_member;

typedef struct xx_fiz_stream_s {
    xx_fiz_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_fiz_stream;

static void xx_fiz_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_fiz_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_fiz_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_fiz_path_safe(const char *name) {
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

static void xx_fiz_stream_free(void *pointer) {
    xx_fiz_stream *stream = (xx_fiz_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_fiz_add(xx_fiz_stream *stream,
                          const xx_fiz_member *member) {
    xx_fiz_member *grown = (xx_fiz_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_FIZ_MAX_MEMBERS 100000
#define XX_FIZ_MIN_LH5_SIZE 2
#define XX_FIZ_HEADER_SIZE 20
#define XX_FIZ_MAX_NAME_SIZE 12
#define XX_FIZ_METHOD_STORED 0U
#define XX_FIZ_METHOD_LH5 1U
#define XX_FIZ_MAX_UNCOMPRESSED ((int64_t)256 * 1024 * 1024)
#define XX_FIZ_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_fiz_le16(const uint8_t *data);
static uint32_t xx_fiz_le32(const uint8_t *data);
static xx_fiz_stream *xx_fiz_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_fiz_decode(Abstractformat *self, const xx_fiz_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* No member count is stored anywhere, so this is a runaway guard rather than
 * a format limit; it matches the reference implementation's bound. */
/* An -lh5- stream always carries at least a 16-bit block count, so a
 * non-empty member cannot have a payload shorter than two bytes. */

static uint16_t xx_fiz_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_fiz_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static xx_fiz_stream *xx_fiz_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_fiz_stream *stream;
    uint8_t header[XX_FIZ_HEADER_SIZE];
    uint8_t name_field[XX_FIZ_MAX_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t count = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Smallest conceivable member: header + a one-character name + one
     * payload byte. Anything shorter cannot be a chain. */
    if (span < XX_FIZ_HEADER_SIZE + 2) return NULL;

    stream = (xx_fiz_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = 0;
    for (;;) {
        xx_fiz_member member;
        char *name;
        uint32_t method;
        int64_t name_size;
        int64_t uncompressed_size;
        int64_t compressed_size;
        int64_t data_offset;
        size_t index;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (count >= XX_FIZ_MAX_MEMBERS) goto fail;
        if (!xx_fiz_range_within(span, offset, XX_FIZ_HEADER_SIZE)) goto fail;
        if (!xx_fiz_read_at(self, self->base_address + offset, header,
                            sizeof(header))) {
            goto fail;
        }

        /* The magic is repeated on EVERY member, not only the first. That
         * repetition, and nothing else, is what makes a truncated or spliced
         * chain fail closed, so it is checked on every iteration. */
        if (header[0] != 'F' || header[1] != 'I' || header[2] != 'Z' ||
            header[3] != 0x1A) {
            goto fail;
        }

        /* Exactly two method values exist; an unknown one is a mis-parse, not
         * a member this reader merely cannot extract. */
        method = (uint32_t)header[4];
        if (method != XX_FIZ_METHOD_STORED && method != XX_FIZ_METHOD_LH5) {
            goto fail;
        }

        name_size = (int64_t)header[5];
        if (name_size < 1 || name_size > XX_FIZ_MAX_NAME_SIZE) goto fail;

        uncompressed_size = (int64_t)xx_fiz_le32(header + 8);
        compressed_size = (int64_t)xx_fiz_le32(header + 12);
        if (uncompressed_size > XX_FIZ_MAX_UNCOMPRESSED ||
            compressed_size > XX_FIZ_MAX_UNCOMPRESSED) {
            goto fail;
        }

        if (!xx_fiz_range_within(span, offset + XX_FIZ_HEADER_SIZE,
                                 name_size)) {
            goto fail;
        }
        /* The name field is exactly name_size bytes and is NOT NUL
         * terminated; read the field, never scan for a terminator. */
        if (!xx_fiz_read_at(self, self->base_address + offset +
                                      XX_FIZ_HEADER_SIZE,
                            name_field, (size_t)name_size)) {
            goto fail;
        }
        for (index = 0U; index < (size_t)name_size; ++index) {
            /* DOS 8.3 names are plain printable ASCII here; the format grants
             * no exemption, and a byte outside the range means the parser has
             * walked into payload. */
            if (name_field[index] < 0x20U || name_field[index] > 0x7EU) {
                goto fail;
            }
        }
        name_field[name_size] = 0U;

        data_offset = offset + XX_FIZ_HEADER_SIZE + name_size;
        /* A member extending past EOF is a rejection, not a short read. */
        if (!xx_fiz_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }

        if (method == XX_FIZ_METHOD_STORED) {
            /* The stored members are the structural anchor that keeps the
             * two-value method gate honest: both size fields must agree. */
            if (compressed_size != uncompressed_size) goto fail;
        } else {
            if (uncompressed_size > 0 &&
                compressed_size < XX_FIZ_MIN_LH5_SIZE) {
                goto fail;
            }
            if (uncompressed_size == 0 && compressed_size != 0) goto fail;
        }

        name = xx_str_dup((const char *)name_field);
        if (!name) goto fail;
        if (!xx_fiz_path_safe(name)) {
            xx_str_free(name);
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_FIZ_HEADER_SIZE + name_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* The container's own method number, unchanged, so a listing shows
         * what the archive actually says. */
        member.method = method;
        /* DOS date/time. The header stores the TIME word first (0x10) and the
         * date word second (0x12), the reverse of the obvious order; swapping
         * them yields plausible nonsense rather than an error. */
        member.timestamp = ((uint64_t)xx_fiz_le16(header + 0x12) << 16) |
                           (uint64_t)xx_fiz_le16(header + 0x10);
        /* The format is flat: there are no directory entries. */
        member.is_folder = false;
        /* The CRC-16 at 0x06 covers the UNPACKED member, so verifying it here
         * would decompress the whole archive on every format probe. */

        if (!xx_fiz_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        ++count;
        offset = data_offset + compressed_size;

        /* There is no terminator record: the chain ends by landing exactly on
         * end-of-file. A short tail is a reject, not an overlay - no FIZ
         * archive in the corpus has slack, and accepting one would turn any
         * four-byte prefix match into a hit. This is the check a later reader
         * will be tempted to loosen. */
        if (offset == span) break;
        if (offset > span) goto fail;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = span;
    return stream;

fail:
    xx_fiz_stream_free(stream);
    return NULL;
}


/* The writer only ever emits 8.3 names, and the reference detector rejects a
 * length byte outside 1..12 (it tests bit nameLen of the mask 0x1ffe). */


/* The container states the plaintext length, and it is attacker-controlled:
 * refuse rather than attempt the allocation. The reference implementation
 * rejects a size field with the sign bit set; the same limit falls out of
 * this 256 MiB cap. */

/* Method 1 is a bare -lh5- bitstream: no LHA header, no level byte, just the
 * blocks. Verified against the reference decompressor, which drives the
 * shared LHA engine with dicbit 13 / np 14 / pbit 4 and NC 510, i.e. plain
 * -lh5- - hence the literal 5 handed to xx_lzh5_decode_memory below. Passing
 * 4, 6 or 7 there would select a different window size and decode to
 * plausible garbage instead of failing. */
static bool xx_fiz_decode(Abstractformat *self, const xx_fiz_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input;
    uint8_t *output;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_FIZ_MAX_DECODED ||
        member->uncompressed_size > XX_FIZ_MAX_DECODED) {
        return false;
    }
    /* A method the format defines but this reader does not implement must
     * fail here: silently treating it as stored produces garbage that looks
     * like data. */
    if (member->method != XX_FIZ_METHOD_STORED &&
        member->method != XX_FIZ_METHOD_LH5) {
        return false;
    }

    if (member->uncompressed_size == 0) {
        /* An empty member is a real, empty file; parse already required the
         * payload to be empty too. */
        if (member->compressed_size != 0) return false;
        output = (uint8_t *)xx_mem_alloc(1U);
        if (!output) return false;
        *out = output;
        *out_size = 0U;
        return true;
    }

    input = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!input) return false;
    if (!xx_fiz_read_at(self, member->data_offset, input,
                        (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    if (member->method == XX_FIZ_METHOD_STORED) {
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
    /* Exactly the stored plaintext length, or nothing: a short decode
     * reported as success is the one failure the caller cannot detect. */
    if (!xx_lzh5_decode_memory(input, (size_t)member->compressed_size, output,
                               (size_t)member->uncompressed_size, 5,
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

void xx_fiz_init(xx_fiz *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_FIZ;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-fiz");
    xx_format_set_extension(&archive->format, "fiz");
    archive->format.check_is_valid = xx_fiz_check_is_valid;
    archive->format.handle_base_info = xx_fiz_handle_base_info;
    archive->format.get_format_size = xx_fiz_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_fiz_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_fiz_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_fiz_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_fiz_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_fiz_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_fiz_free_archive_records_reading;
    archive->format.destroy = xx_fiz_vtable_destroy;
}

xx_fiz *xx_fiz_create(xx_io_device *device, int64_t base_address) {
    xx_fiz *archive = (xx_fiz *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_fiz_init(archive, device, base_address);
    return archive;
}

void xx_fiz_destroy(xx_fiz *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_fiz_free(xx_fiz *archive) {
    if (!archive) return;
    xx_fiz_destroy(archive);
    xx_mem_free(archive);
}

static void xx_fiz_vtable_destroy(Abstractformat *self) {
    xx_fiz_destroy((xx_fiz *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_fiz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_fiz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_fiz_parse(self, pd);
    if (!stream) return false;
    xx_fiz_stream_free(stream);
    return true;
}

bool xx_fiz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_fiz *archive = (xx_fiz *)self;
    xx_fiz_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_fiz_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_fiz_stream_free(stream);
    return true;
}

int64_t xx_fiz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_fiz_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_fiz *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_fiz_set_record(xx_archive_record *record,
                                 const xx_fiz_member *member) {
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

static bool xx_fiz_copy_options(xx_list_s *target,
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

static const xx_var *xx_fiz_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_fiz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_fiz_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_fiz_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_fiz_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_fiz_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_fiz_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_fiz_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_fiz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_fiz_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_fiz_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_fiz_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_fiz_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_fiz_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_fiz_stream *stream;
    const xx_fiz_member *member;
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
    stream = (xx_fiz_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_fiz_path_safe(member->name)) return false;

    path_option = xx_fiz_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_fiz_decode(self, member, &plain, &plain_size, pd);
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
        !xx_fiz_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_fiz_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
