/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MVA installer archives (.mva, with .mvb continuation volumes).
 *
 * Container header, 8 bytes at offset 0:
 *
 *   0x00  "mflh"
 *   0x04  u32 LE container version, always 1
 *
 * Then a chain of members, each a fixed 0x15a-byte header immediately
 * followed by its data.  There is no directory and no count: the next member
 * header sits at data_offset + compressed_size, and the chain ends at EOF or
 * at the first header that does not carry the member tag.
 *
 * Member header, 0x15a bytes:
 *
 *   0x000  "mfen"
 *   0x004  u16 LE member version, always 1
 *   0x006  u16 LE header size, always 0x15a
 *   0x008  u32 LE modification time, Unix seconds
 *   0x00c  u32 LE creation time, Unix seconds
 *   0x010  path, a fixed 260-byte buffer, NUL terminated and NUL padded
 *   0x114  i32 LE uncompressed size
 *   0x118  i32 LE compressed size
 *   0x11c  u32 LE checksum 1
 *   0x120  u32 LE checksum 2
 *   0x124  u32 LE attributes
 *   0x128  unused to the end of the header
 *   0x15a  member data, compressed_size bytes
 *
 * There is no compression-method field.  A member is stored when its two
 * sizes are equal and a zlib (RFC 1950) stream otherwise, which is the only
 * thing the writer ever emits.
 *
 * The stored paths are absolute build-machine paths such as
 * "C:\Build Win95 drv\input\m_qdesk.dll"; members are reported by base name
 * only, so nothing resembling a drive letter or an absolute path reaches the
 * extraction directory.
 *
 * The format's defence against a false positive is not the four-byte
 * container tag but the first member header that must follow it: "mfen", a
 * version of exactly 1, and a self-described header size of exactly 0x15a,
 * plus a printable path field.  A file with no valid member header yields an
 * empty member list and is rejected.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/mva/xx_mva.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"

#include <stdio.h>

#define XX_MVA_COPY_CHUNK (64 * 1024)

typedef struct xx_mva_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_mva_member;

typedef struct xx_mva_stream_s {
    xx_mva_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_mva_stream;

static void xx_mva_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_mva_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_mva_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_mva_path_safe(const char *name) {
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

static void xx_mva_stream_free(void *pointer) {
    xx_mva_stream *stream = (xx_mva_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_mva_add(xx_mva_stream *stream,
                          const xx_mva_member *member) {
    xx_mva_member *grown = (xx_mva_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_MVA_CONTAINER_HEADER_SIZE 8
#define XX_MVA_MEMBER_HEADER_SIZE 0x15a
#define XX_MVA_NAME_OFFSET 0x10
#define XX_MVA_NAME_SIZE 260
#define XX_MVA_SIZES_OFFSET 0x114
#define XX_MVA_MEMBER_VERSION 1U
#define XX_MVA_CONTAINER_VERSION 1U
#define XX_MVA_MAX_MEMBERS 100000
#define XX_MVA_MAX_MEMBER_SIZE 0x40000000
#define XX_MVA_METHOD_STORE 0U
#define XX_MVA_METHOD_ZLIB 8U
#define XX_MVA_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_mva_le16(const uint8_t *data);
static uint32_t xx_mva_le32(const uint8_t *data);
static size_t xx_mva_field_length(const uint8_t *field);
static bool xx_mva_path_valid(const uint8_t *field, size_t length);
static char *xx_mva_base_name(const uint8_t *field, size_t length);
static xx_mva_stream *xx_mva_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_mva_decode(Abstractformat *self, const xx_mva_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* 1 GB: no member of a real installer volume approaches this, and the cap
 * keeps a garbage size from being carried into the chain arithmetic. */

static uint16_t xx_mva_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_mva_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* The path is a fixed 260-byte buffer.  Some writers leave stale bytes after
 * the terminator, so only the run up to the first NUL is the name; a field
 * with no terminator at all is the whole 260 bytes, as the reference
 * implementation reads it. */
static size_t xx_mva_field_length(const uint8_t *field) {
    size_t index;

    for (index = 0U; index < (size_t)XX_MVA_NAME_SIZE; ++index) {
        if (field[index] == 0U) return index;
    }
    return (size_t)XX_MVA_NAME_SIZE;
}

/* Together with the member tag and the self-described header size, this is
 * what keeps an arbitrary run of bytes from reading as a member header: 260
 * bytes of plausible path text are not something unrelated data supplies.
 * Loosening it turns any file whose first bytes happen to be "mflh" into a
 * candidate archive.
 *
 * Bytes 0x7F..0xFF are deliberately permitted: the paths are raw DOS/Latin-1
 * build-machine paths and the reference validator accepts them.  The
 * punctuation refused below is what Windows forbids in a path and what a
 * writer therefore never emits. */
static bool xx_mva_path_valid(const uint8_t *field, size_t length) {
    size_t index;

    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t byte = field[index];

        if (byte < 0x20U) return false;
        if (byte == '<' || byte == '>' || byte == '"' || byte == '|' ||
            byte == '*' || byte == '?') {
            return false;
        }
    }
    return true;
}

/* The stored path is absolute and drive qualified.  Members are reported by
 * base name only, matching the reference, which also means no separator, no
 * drive letter and no leading '/' can reach the extraction path. */
static char *xx_mva_base_name(const uint8_t *field, size_t length) {
    size_t start = 0U;
    size_t index;
    size_t size;
    char *result;

    for (index = 0U; index < length; ++index) {
        if (field[index] == '/' || field[index] == '\\' ||
            field[index] == ':') {
            start = index + 1U;
        }
    }
    size = length - start;
    if (size == 0U) return NULL;
    result = (char *)xx_mem_alloc(size + 1U);
    if (!result) return NULL;
    for (index = 0U; index < size; ++index) {
        result[index] = (char)field[start + index];
    }
    result[size] = '\0';
    return result;
}

static xx_mva_stream *xx_mva_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_mva_stream *stream = NULL;
    uint8_t header[XX_MVA_MEMBER_HEADER_SIZE];
    const uint8_t *field = header + XX_MVA_NAME_OFFSET;
    int64_t total;
    int64_t span;
    int64_t offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* A volume always holds the container header and at least one complete
     * member header. */
    if (span < XX_MVA_CONTAINER_HEADER_SIZE + XX_MVA_MEMBER_HEADER_SIZE) {
        return NULL;
    }
    if (!xx_mva_read_at(self, self->base_address, header,
                        (size_t)XX_MVA_CONTAINER_HEADER_SIZE)) {
        return NULL;
    }
    if (header[0] != 'm' || header[1] != 'f' || header[2] != 'l' ||
        header[3] != 'h') {
        return NULL;
    }
    /* A .MVB continuation volume repeats the "mflh" tag but carries the tail
     * of the previous volume's stream here instead of 1, so the version is
     * what separates a volume that can be parsed from one that cannot. */
    if (xx_mva_le32(header + 4) != XX_MVA_CONTAINER_VERSION) return NULL;

    stream = (xx_mva_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_MVA_CONTAINER_HEADER_SIZE;
    while (offset < span) {
        xx_mva_member member;
        size_t name_length;
        int64_t data_offset;
        int64_t uncompressed;
        int64_t compressed;
        char *name;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_MVA_MAX_MEMBERS) goto fail;
        /* A truncated trailing header means the volume was cut; everything
         * parsed so far is still valid, so stop rather than fail. */
        if (!xx_mva_range_within(span, offset, XX_MVA_MEMBER_HEADER_SIZE)) {
            break;
        }
        if (!xx_mva_read_at(self, self->base_address + offset, header,
                            (size_t)XX_MVA_MEMBER_HEADER_SIZE)) {
            goto fail;
        }
        /* End of the chain: the bytes after the last member are whatever the
         * installer appended, not another header. */
        if (header[0] != 'm' || header[1] != 'f' || header[2] != 'e' ||
            header[3] != 'n') {
            break;
        }
        if (xx_mva_le16(header + 4) != XX_MVA_MEMBER_VERSION) break;
        /* The header describes its own length, and every writer emits 0x15a.
         * Requiring the exact value is what makes the four-byte member tag
         * meaningful. */
        if ((int64_t)xx_mva_le16(header + 6) != XX_MVA_MEMBER_HEADER_SIZE) {
            break;
        }

        /* Both sizes are signed in the container; a negative one is a
         * corrupt header, not a large member. */
        uncompressed =
            (int64_t)(int32_t)xx_mva_le32(header + XX_MVA_SIZES_OFFSET);
        compressed =
            (int64_t)(int32_t)xx_mva_le32(header + XX_MVA_SIZES_OFFSET + 4);
        if (uncompressed < 0 || compressed < 0) break;
        if (uncompressed > XX_MVA_MAX_MEMBER_SIZE ||
            compressed > XX_MVA_MAX_MEMBER_SIZE) {
            break;
        }

        name_length = xx_mva_field_length(field);
        if (!xx_mva_path_valid(field, name_length)) break;
        name = xx_mva_base_name(field, name_length);
        /* A path ending in a separator has no base name; the reference stops
         * the chain there too. */
        if (!name) break;

        data_offset = offset + XX_MVA_MEMBER_HEADER_SIZE;
        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_MVA_MEMBER_HEADER_SIZE;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed;
        member.uncompressed_size = uncompressed;
        member.timestamp = (uint64_t)xx_mva_le32(header + 8);
        /* No method field exists: equal sizes mean the writer stored the
         * member verbatim, anything else is a zlib stream. */
        member.method = (compressed == uncompressed) ? XX_MVA_METHOD_STORE
                                                     : XX_MVA_METHOD_ZLIB;

        if (!xx_mva_range_within(span, data_offset, compressed)) {
            /* The last member of a volume that spills into a .MVB is
             * truncated here; publish what this volume actually holds
             * instead of dropping it.  It is no longer a whole stream, so it
             * must not be reported as stored. */
            member.compressed_size = span - data_offset;
            member.method = XX_MVA_METHOD_ZLIB;
            if (!xx_mva_add(stream, &member)) {
                xx_str_free(name);
                goto fail;
            }
            offset = span;
            break;
        }
        if (!xx_mva_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        offset = data_offset + compressed;
    }

    /* No member header at all: the container tag was a coincidence. */
    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* Bytes past the last member are an overlay, not part of the archive. */
    stream->archive_size = offset;
    return stream;

fail:
    xx_mva_stream_free(stream);
    return NULL;
}


/* The container carries no method field; these are what parse derives from
 * the two sizes, and the only two values a member can hold. */
/* A member's sizes are attacker controlled, so both the packed read and the
 * decoded output are refused above this rather than attempted. */

static bool xx_mva_decode(Abstractformat *self, const xx_mva_member *member,
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
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_MVA_MAX_DECODED ||
        member->uncompressed_size > XX_MVA_MAX_DECODED) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;
    packed_size = (size_t)member->compressed_size;
    plain_size = (size_t)member->uncompressed_size;

    if (member->method == XX_MVA_METHOD_STORE) {
        /* parse only assigns STORE when the sizes agree; re-checking here
         * keeps a hand-edited member from copying a short stream out under
         * the longer size the listing showed. */
        if (member->compressed_size != member->uncompressed_size) return false;
        plain = (uint8_t *)xx_mem_alloc(plain_size != 0U ? plain_size : 1U);
        if (!plain) return false;
        if (plain_size != 0U &&
            !xx_mva_read_at(self, member->data_offset, plain, plain_size)) {
            xx_mem_free(plain);
            return false;
        }
        if (pd && xx_pd_is_stopped(pd)) {
            xx_mem_free(plain);
            return false;
        }
        *out = plain;
        *out_size = plain_size;
        return true;
    }

    /* Anything that is not the one compressed method this format defines is
     * a refusal: treating it as stored would hand the caller packed bytes
     * that look like data. */
    if (member->method != XX_MVA_METHOD_ZLIB) return false;
    /* A zlib stream is never empty, and it never decodes to nothing here: a
     * member with either size zero would have been stored. */
    if (packed_size == 0U || plain_size == 0U) return false;

    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    if (!xx_mva_read_at(self, member->data_offset, packed, packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }
    /* The last member of a volume that spills into a .MVB is published with
     * its compressed size clamped to the end of this volume; the header check
     * still passes there, but the decode below stops short and the size
     * comparison rejects it rather than reporting a partial file. */
    if (!xx_zlib_stream_header_is_valid(packed, packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    if (!xx_zlib_stream_decode_memory(packed, packed_size, plain, plain_size,
                                      &written) ||
        written != plain_size) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = plain_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_mva_init(xx_mva *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_MVA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-mva");
    xx_format_set_extension(&archive->format, "mva");
    archive->format.check_is_valid = xx_mva_check_is_valid;
    archive->format.handle_base_info = xx_mva_handle_base_info;
    archive->format.get_format_size = xx_mva_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_mva_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_mva_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_mva_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_mva_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_mva_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_mva_free_archive_records_reading;
    archive->format.destroy = xx_mva_vtable_destroy;
}

xx_mva *xx_mva_create(xx_io_device *device, int64_t base_address) {
    xx_mva *archive = (xx_mva *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_mva_init(archive, device, base_address);
    return archive;
}

void xx_mva_destroy(xx_mva *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_mva_free(xx_mva *archive) {
    if (!archive) return;
    xx_mva_destroy(archive);
    xx_mem_free(archive);
}

static void xx_mva_vtable_destroy(Abstractformat *self) {
    xx_mva_destroy((xx_mva *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_mva_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_mva_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_mva_parse(self, pd);
    if (!stream) return false;
    xx_mva_stream_free(stream);
    return true;
}

bool xx_mva_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_mva *archive = (xx_mva *)self;
    xx_mva_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_mva_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_mva_stream_free(stream);
    return true;
}

int64_t xx_mva_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_mva_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_mva *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_mva_set_record(xx_archive_record *record,
                                 const xx_mva_member *member) {
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

static bool xx_mva_copy_options(xx_list_s *target,
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

static const xx_var *xx_mva_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_mva_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_mva_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_mva_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mva_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_mva_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_mva_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_mva_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_mva_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_mva_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_mva_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_mva_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_mva_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_mva_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_mva_stream *stream;
    const xx_mva_member *member;
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
    stream = (xx_mva_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_mva_path_safe(member->name)) return false;

    path_option = xx_mva_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_mva_decode(self, member, &plain, &plain_size, pd);
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
        !xx_mva_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_mva_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
