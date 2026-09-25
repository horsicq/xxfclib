/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Electronic Arts DOS archives (.PEA).
 *
 * There is no archive-wide header and no central directory. Members are laid
 * end to end from the start of the format: a fixed 48-byte header, then the
 * payload, then the next header. Every field of the header is accounted for.
 *
 *   0x00  u8       0x1A -- the DOS end-of-file byte, used as a magic byte
 *   0x01  char[2]  "EA"
 *   0x03  char[12] member name, an 8.3 DOS name, NUL terminated
 *   0x0F  u8       always 0 -- the terminator of a maximum-length name
 *   0x10  u32 LE   a stamp of some kind; NOT a DOS date (see below)
 *   0x14  u8       method: 0 = stored, 1 = 12-bit LZW
 *   0x15  u32 LE   uncompressed size, read signed, never negative
 *   0x19  u32 LE   compressed size, read signed, never negative
 *   0x1D  u32 LE   format tag, the constant 0x00000130
 *   0x21  u8[15]   zero padding, zero on every member of the corpus
 *   0x30           the payload begins here
 *
 * The name field is NUL padded, but the padding is NOT clean: the packer
 * overwrites the fixed-width field without clearing it, so the bytes behind
 * the terminating NUL are the tail of the previous, longer name. Only the
 * bytes in front of the NUL can be validated -- which is exactly why the
 * constant tag at 0x1D and the fifteen zero bytes at 0x21 carry the weight
 * of the detection.
 *
 * The stamp at 0x10 does not decode as an MS-DOS date on most members (the
 * year lands far outside 1980..2107), so it is published raw rather than as
 * a timestamp.
 *
 * The chain has no terminator record. It is accepted only when the last
 * payload ends exactly on EOF; a chain that would run past EOF, or that
 * stops short of it, is a rejection rather than a truncated archive.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ea/xx_ea.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/ea/xx_ea.h"

#include <stdio.h>

#define XX_EA_COPY_CHUNK (64 * 1024)

typedef struct xx_ea_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_ea_member;

typedef struct xx_ea_stream_s {
    xx_ea_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_ea_stream;

static void xx_ea_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_ea_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_ea_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_ea_path_safe(const char *name) {
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

static void xx_ea_stream_free(void *pointer) {
    xx_ea_stream *stream = (xx_ea_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_ea_add(xx_ea_stream *stream,
                          const xx_ea_member *member) {
    xx_ea_member *grown = (xx_ea_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_EA_HEADER_SIZE 48
#define XX_EA_NAME_OFFSET 3
#define XX_EA_NAME_SIZE 12
#define XX_EA_STAMP_OFFSET 0x10
#define XX_EA_METHOD_OFFSET 0x14
#define XX_EA_USIZE_OFFSET 0x15
#define XX_EA_CSIZE_OFFSET 0x19
#define XX_EA_TAG_OFFSET 0x1d
#define XX_EA_ZEROS_OFFSET 0x21
#define XX_EA_ZEROS_SIZE 15
#define XX_EA_TAG_VALUE 0x00000130UL
#define XX_EA_METHOD_STORED 0U
#define XX_EA_METHOD_LZW 1U
#define XX_EA_MAX_MEMBERS 100000
#define XX_EA_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_ea_le32(const uint8_t *data);
static bool xx_ea_decode_name(const uint8_t *header, char *name);
static xx_ea_stream *xx_ea_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_ea_decode(Abstractformat *self, const xx_ea_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Constant on every member of every known archive; the reference reader
 * reports it as the format version. */

static uint32_t xx_ea_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The name is a NUL-padded 8.3 DOS name. Only the bytes in FRONT of the NUL
 * may be checked: the packer reuses the fixed-width field without clearing
 * it, so whatever follows the terminator is the tail of a previous, longer
 * name. Demanding clean padding here rejected 12 of the 27 reference
 * archives, so it is deliberately not demanded. */
static bool xx_ea_decode_name(const uint8_t *header, char *name) {
    size_t length = 0U;
    size_t index;

    while (length < (size_t)XX_EA_NAME_SIZE &&
           header[XX_EA_NAME_OFFSET + length] != 0U) {
        ++length;
    }
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t byte = header[XX_EA_NAME_OFFSET + index];

        /* A DOS 8.3 name is printable ASCII and carries no path component;
         * a separator would let a member escape the output directory. */
        if (byte < 0x20U || byte > 0x7eU) return false;
        if (byte == '/' || byte == '\\' || byte == ':') return false;
        name[index] = (char)byte;
    }
    name[length] = '\0';
    return true;
}

static xx_ea_stream *xx_ea_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_ea_stream *stream = NULL;
    uint8_t header[XX_EA_HEADER_SIZE];
    char name[XX_EA_NAME_SIZE + 1];
    int64_t total;
    int64_t span;
    int64_t offset = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_EA_HEADER_SIZE) return NULL;

    stream = (xx_ea_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (stream->count < (size_t)XX_EA_MAX_MEMBERS) {
        xx_ea_member member;
        uint32_t method;
        int64_t uncompressed;
        int64_t compressed;
        int64_t data_offset;
        int index;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_ea_range_within(span, offset, XX_EA_HEADER_SIZE)) goto fail;
        if (!xx_ea_read_at(self, self->base_address + offset, header,
                           (size_t)XX_EA_HEADER_SIZE)) {
            goto fail;
        }

        /* 0x1A "EA" is only three bytes and the leading byte is the DOS EOF
         * marker, which is common in ordinary data. */
        if (header[0] != 0x1aU) goto fail;
        if (header[1] != 'E' || header[2] != 'A') goto fail;
        /* The constant tag and the fifteen zero bytes behind it are the real
         * defence against a false positive: they are the only fixed content
         * in the header that a chance "1A 45 41" hit will not reproduce, and
         * they have to carry that weight because the name field cannot be
         * checked past its NUL. Loosening either turns every stray DOS EOF
         * byte followed by "EA" into an archive. */
        if (xx_ea_le32(header + XX_EA_TAG_OFFSET) !=
            (uint32_t)XX_EA_TAG_VALUE) {
            goto fail;
        }
        if (header[XX_EA_NAME_OFFSET + XX_EA_NAME_SIZE] != 0U) goto fail;
        for (index = 0; index < XX_EA_ZEROS_SIZE; ++index) {
            if (header[XX_EA_ZEROS_OFFSET + index] != 0U) goto fail;
        }

        method = (uint32_t)header[XX_EA_METHOD_OFFSET];
        if (method != XX_EA_METHOD_STORED && method != XX_EA_METHOD_LZW) {
            goto fail;
        }

        /* Both size words are read as SIGNED 32-bit and a negative one is a
         * rejection; that is the only bound the format itself states. The
         * ceiling on the uncompressed size is this reader's own sanity cap. */
        uncompressed = (int64_t)(int32_t)xx_ea_le32(header +
                                                    XX_EA_USIZE_OFFSET);
        compressed = (int64_t)(int32_t)xx_ea_le32(header +
                                                  XX_EA_CSIZE_OFFSET);
        if (uncompressed < 0 || compressed < 0) goto fail;
        if (uncompressed > XX_EA_MAX_DECODED) goto fail;

        if (!xx_ea_decode_name(header, name)) goto fail;

        data_offset = offset + XX_EA_HEADER_SIZE;
        if (!xx_ea_range_within(span, data_offset, compressed)) goto fail;
        /* A stored member is the structural anchor of the method gate: with
         * no compression the two size fields describe the same bytes, so a
         * disagreement means this is not an EA header. */
        if (method == XX_EA_METHOD_STORED && compressed != uncompressed) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_EA_HEADER_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed;
        member.uncompressed_size = uncompressed;
        member.method = method;
        /* Published raw: +0x10 is not an MS-DOS date on most members, so
         * turning it into a timestamp would invent information. */
        member.timestamp = (uint64_t)xx_ea_le32(header + XX_EA_STAMP_OFFSET);
        member.is_folder = false;
        if (!xx_ea_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset = data_offset + compressed;
        /* There is no terminator record: the chain is valid only when it
         * lands EXACTLY on EOF. Accepting a chain that stops short would let
         * any three-byte coincidence followed by plausible sizes parse as a
         * one-member archive with the rest of the file as overlay. */
        if (offset == span) {
            stream->archive_size = span;
            if (pd && xx_pd_is_stopped(pd)) goto fail;
            return stream;
        }
    }

fail:
    xx_ea_stream_free(stream);
    return NULL;
}


/* The stored uncompressed size is attacker-controlled -- the field is a full
 * 32-bit word -- so it is capped before it ever becomes an allocation. The
 * parse applies the same cap, which keeps a listing and an extraction from
 * disagreeing about which members exist. */

static bool xx_ea_decode(Abstractformat *self, const xx_ea_member *member,
                         uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t plain_size;
    size_t written = 0U;

    if (!out || !out_size) return false;
    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_EA_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;
    /* Method 1 is the only compressed method the format defines, and an LZW
     * stream always emits at least a CLEAR and one literal, so neither an
     * empty payload nor an empty result is representable. */
    if (member->method == XX_EA_METHOD_LZW &&
        (member->compressed_size == 0 || member->uncompressed_size == 0)) {
        return false;
    }
    if (member->method != XX_EA_METHOD_STORED &&
        member->method != XX_EA_METHOD_LZW) {
        return false;
    }
    if (member->method == XX_EA_METHOD_STORED &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    if (member->compressed_size > 0) {
        packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
        if (!packed) return false;
        if (!xx_ea_read_at(self, member->data_offset, packed,
                           (size_t)member->compressed_size)) {
            xx_mem_free(packed);
            return false;
        }
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    /* xx_mem_alloc(0) hands back NULL, which the caller cannot tell from a
     * failure, so a legitimately empty stored member still gets one byte. */
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_EA_METHOD_STORED) {
        size_t index;

        for (index = 0U; index < plain_size; ++index) {
            plain[index] = packed[index];
        }
        written = plain_size;
    } else if (!xx_ea_decode_memory(packed, (size_t)member->compressed_size,
                                    plain, plain_size, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* Returning true with fewer bytes than the header promised is the one
     * failure the caller cannot detect. The LZW stream carries no length of
     * its own, so this comparison against the container is the only thing
     * that catches a stream that stopped early. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_ea_init(xx_ea *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_EA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ea-pea");
    xx_format_set_extension(&archive->format, "pea");
    archive->format.check_is_valid = xx_ea_check_is_valid;
    archive->format.handle_base_info = xx_ea_handle_base_info;
    archive->format.get_format_size = xx_ea_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ea_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ea_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ea_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ea_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ea_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ea_free_archive_records_reading;
    archive->format.destroy = xx_ea_vtable_destroy;
}

xx_ea *xx_ea_create(xx_io_device *device, int64_t base_address) {
    xx_ea *archive = (xx_ea *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ea_init(archive, device, base_address);
    return archive;
}

void xx_ea_destroy(xx_ea *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ea_free(xx_ea *archive) {
    if (!archive) return;
    xx_ea_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ea_vtable_destroy(Abstractformat *self) {
    xx_ea_destroy((xx_ea *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_ea_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ea_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ea_parse(self, pd);
    if (!stream) return false;
    xx_ea_stream_free(stream);
    return true;
}

bool xx_ea_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ea *archive = (xx_ea *)self;
    xx_ea_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_ea_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_ea_stream_free(stream);
    return true;
}

int64_t xx_ea_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ea_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_ea *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_ea_set_record(xx_archive_record *record,
                                 const xx_ea_member *member) {
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

static bool xx_ea_copy_options(xx_list_s *target,
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

static const xx_var *xx_ea_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_ea_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ea_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ea_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ea_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ea_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ea_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ea_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ea_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ea_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ea_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_ea_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ea_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_ea_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ea_stream *stream;
    const xx_ea_member *member;
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
    stream = (xx_ea_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_ea_path_safe(member->name)) return false;

    path_option = xx_ea_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_ea_decode(self, member, &plain, &plain_size, pd);
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
        !xx_ea_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_ea_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
