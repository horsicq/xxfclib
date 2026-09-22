/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DS'L install 2.0 archives (*.d00).
 *
 * Header, 0x1c bytes at the start of the format:
 *
 *   0x00  16 bytes  "DS'L install 2.0"
 *   0x10  u16 LE    format revision, always 0
 *   0x12  u16 LE    number of members, at least 1
 *   0x14  u32 LE    unused by this reader
 *   0x18  u32 LE    end offset of the directory, file relative
 *
 * Two length-prefixed install paths follow at 0x1c, each a u16 LE byte count
 * and that many bytes, stored in the clear. The directory starts where the
 * second one ends and runs to the end offset from the header; there is no
 * field giving its start, so the two paths have to be walked to find it.
 *
 * The directory is obfuscated: every byte is stored biased by +0x33, so the
 * whole block is read and 0x33 subtracted from each byte before any record is
 * parsed. Records are variable length and packed back to back:
 *
 *   0x00  u16 LE  record size, including this header and the name
 *   0x02  u32 LE  uncompressed size
 *   0x06  u32 LE  compressed size
 *   0x0a  u32 LE  data offset, file relative
 *   0x0e  u16 LE  DOS time
 *   0x10  u16 LE  DOS date
 *   0x12  u16 LE  flags; bit 4 (0x0010) means PKWARE DCL imploded
 *   0x14  u16 LE  name length, counting its own terminating NUL
 *   0x16  ...     name bytes
 *
 * The minimum record is 0x14 bytes, which is two bytes short of the name
 * length field: a record can legitimately stop before it, and such a record
 * simply has no name. The name itself is bounded by the end of the directory
 * block rather than by the record, because that is where the reference reads
 * it from -- a name may run past the record size its own header declares.
 *
 * The format's defence against a false positive is the 16-byte magic; nothing
 * downstream is nearly as selective, so the magic must stay an exact match.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dsl2/xx_dsl2.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/dcl/xx_dcl.h"

#include <stdio.h>

#define XX_DSL2_COPY_CHUNK (64 * 1024)

typedef struct xx_dsl2_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_dsl2_member;

typedef struct xx_dsl2_stream_s {
    xx_dsl2_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_dsl2_stream;

static void xx_dsl2_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_dsl2_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_dsl2_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_dsl2_path_safe(const char *name) {
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

static void xx_dsl2_stream_free(void *pointer) {
    xx_dsl2_stream *stream = (xx_dsl2_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_dsl2_add(xx_dsl2_stream *stream,
                          const xx_dsl2_member *member) {
    xx_dsl2_member *grown = (xx_dsl2_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_DSL2_MAGIC_SIZE 16
#define XX_DSL2_HEADER_SIZE 0x1c
#define XX_DSL2_ENTRY_MIN_SIZE 0x14
#define XX_DSL2_NAME_OFFSET 0x16
#define XX_DSL2_MAX_MEMBERS 100000
#define XX_DSL2_MAX_DIRECTORY_SIZE 0x1000000
#define XX_DSL2_OBFUSCATION_BIAS 0x33
#define XX_DSL2_MAX_NAME 1024
#define XX_DSL2_FLAG_COMPRESSED 0x0010U
#define XX_DSL2_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_dsl2_le16(const uint8_t *data);
static uint32_t xx_dsl2_le32(const uint8_t *data);
static char *xx_dsl2_make_name(const uint8_t *raw, size_t size, size_t index);
static xx_dsl2_stream *xx_dsl2_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_dsl2_decode(Abstractformat *self, const xx_dsl2_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The directory is stored with every byte biased by this amount. */

static uint16_t xx_dsl2_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_dsl2_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Names are DOS paths written with backslashes; they are translated to '/' so
 * the generated extraction path check sees the separators it understands and
 * a ".." component cannot slip through disguised as "..\\".
 *
 * A record whose name length is zero is legal -- the minimum record stops
 * before the name length field -- but the extractor needs something to write,
 * so an unnamed member gets a stable synthetic name instead of an empty one.
 *
 * Bytes outside 0x20..0x7e are rejected: the field is Latin-1 DOS text and
 * the corpus never uses anything else, so a control byte here means the
 * deobfuscated block is not a directory at all. */
static char *xx_dsl2_make_name(const uint8_t *raw, size_t size,
                               size_t index) {
    char buffer[XX_DSL2_MAX_NAME];
    size_t position = 0U;

    if (size >= (size_t)XX_DSL2_MAX_NAME) return NULL;
    while (position < size) {
        uint8_t byte = raw[position];

        /* The stored length counts the terminating NUL and a writer may pad a
         * shorter name out, so the name ends at the first NUL. */
        if (byte == 0U) break;
        if (byte < 0x20U || byte > 0x7eU) return NULL;
        buffer[position] = (byte == '\\') ? '/' : (char)byte;
        ++position;
    }
    buffer[position] = '\0';
    if (position == 0U) {
        xx_rt_snprintf(buffer, sizeof(buffer), "member%u", (unsigned)index);
    }
    return xx_str_dup(buffer);
}

static xx_dsl2_stream *xx_dsl2_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const char magic[XX_DSL2_MAGIC_SIZE] = {
        'D', 'S', '\'', 'L', ' ', 'i', 'n', 's', 't', 'a', 'l', 'l',
        ' ', '2', '.', '0'};
    xx_dsl2_stream *stream = NULL;
    uint8_t *directory = NULL;
    uint8_t header[XX_DSL2_HEADER_SIZE];
    uint8_t length_field[2];
    int64_t total;
    int64_t span;
    int64_t directory_end;
    int64_t directory_start;
    int64_t directory_size;
    int64_t archive_size;
    int64_t position;
    int64_t left;
    int32_t member_count;
    int32_t index;
    size_t scan;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_DSL2_HEADER_SIZE) return NULL;
    if (!xx_dsl2_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    /* This is the format's only real defence against a false positive:
     * everything after it is plain little-endian integers that arbitrary
     * bytes can satisfy. The magic must stay an exact 16-byte match --
     * shortening it to "DS'L" would make any file starting with those four
     * bytes a candidate archive. */
    if (xx_rt_memcmp(header, magic, (size_t)XX_DSL2_MAGIC_SIZE) != 0) {
        return NULL;
    }
    /* The revision word is zero in every 2.0 archive; a nonzero value means a
     * layout this reader has not seen, not a variant to parse anyway. */
    if (xx_dsl2_le16(header + 0x10) != 0U) return NULL;

    member_count = (int32_t)xx_dsl2_le16(header + 0x12);
    /* Read as a signed 32-bit value on purpose: the reference does, so an end
     * offset with the high bit set is a rejection rather than a huge span. */
    directory_end = (int64_t)(int32_t)xx_dsl2_le32(header + 0x18);
    if (member_count < 1 || member_count > XX_DSL2_MAX_MEMBERS) return NULL;
    if (directory_end <= 0 || directory_end > span) return NULL;

    /* Two length-prefixed install paths sit between the header and the
     * directory, stored in the clear. Nothing records where the directory
     * begins, so they have to be stepped over to find it. */
    directory_start = XX_DSL2_HEADER_SIZE;
    for (index = 0; index < 2; ++index) {
        if (!xx_dsl2_range_within(span, directory_start, 2)) return NULL;
        if (!xx_dsl2_read_at(self, self->base_address + directory_start,
                             length_field, sizeof(length_field))) {
            return NULL;
        }
        directory_start += 2 + (int64_t)xx_dsl2_le16(length_field);
    }
    /* A path length long enough to carry the start past the directory end is
     * the sign that the "paths" were not paths. */
    if (directory_start > directory_end) return NULL;

    directory_size = directory_end - directory_start;
    if (directory_size > XX_DSL2_MAX_DIRECTORY_SIZE) return NULL;
    /* member_count is at least one, so the block must hold at least one
     * minimum-size record. */
    if (directory_size < XX_DSL2_ENTRY_MIN_SIZE) return NULL;

    directory = (uint8_t *)xx_mem_alloc((size_t)directory_size);
    if (!directory) return NULL;
    if (!xx_dsl2_read_at(self, self->base_address + directory_start, directory,
                         (size_t)directory_size)) {
        xx_mem_free(directory);
        return NULL;
    }
    for (scan = 0U; scan < (size_t)directory_size; ++scan) {
        directory[scan] =
            (uint8_t)(directory[scan] - (uint8_t)XX_DSL2_OBFUSCATION_BIAS);
    }

    stream = (xx_dsl2_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    /* The directory ends the structural part of the file; member data may
     * extend past it, so the archive size grows to cover the furthest
     * member. */
    archive_size = directory_end;
    position = 0;
    left = directory_size;

    for (index = 0; index < member_count; ++index) {
        const uint8_t *entry;
        xx_dsl2_member member;
        int64_t record_size;
        int64_t uncompressed_size;
        int64_t compressed_size;
        int64_t data_offset;
        int64_t name_offset;
        int64_t name_size;
        int64_t data_end;
        uint16_t flags;
        char *name;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (left < XX_DSL2_ENTRY_MIN_SIZE) goto fail;

        entry = directory + position;
        record_size = (int64_t)xx_dsl2_le16(entry);
        /* The record size walks the directory; a record shorter than the
         * fixed part, or longer than what is left, would desynchronise every
         * record after it. */
        if (record_size < XX_DSL2_ENTRY_MIN_SIZE || record_size > left) {
            goto fail;
        }

        /* All three are signed 32-bit in the reference, so a value with the
         * high bit set is a rejection and not a four-gigabyte member. */
        uncompressed_size = (int64_t)(int32_t)xx_dsl2_le32(entry + 0x02);
        compressed_size = (int64_t)(int32_t)xx_dsl2_le32(entry + 0x06);
        data_offset = (int64_t)(int32_t)xx_dsl2_le32(entry + 0x0a);
        if (uncompressed_size < 0 || compressed_size < 0 || data_offset < 0) {
            goto fail;
        }

        flags = xx_dsl2_le16(entry + 0x12);
        if (flags & XX_DSL2_FLAG_COMPRESSED) {
            if (!xx_dsl2_range_within(span, data_offset, compressed_size)) {
                goto fail;
            }
        } else {
            /* A stored member has no second size; the two fields must agree
             * or the record does not describe what is on disk. This is the
             * cheapest structural test the format offers and it rejects most
             * directories that are really unrelated bytes. */
            if (compressed_size != uncompressed_size) goto fail;
            if (!xx_dsl2_range_within(span, data_offset, uncompressed_size)) {
                goto fail;
            }
        }

        /* The name length field sits two bytes past the end of a minimum
         * record, so it exists only when the directory block reaches it. The
         * name is bounded by the block, not by the record: the reference
         * reads it straight out of the deobfuscated block, so a name may run
         * past the record size its own header declares. */
        name_offset = position + XX_DSL2_NAME_OFFSET;
        name_size = 0;
        if (name_offset <= directory_size) {
            int64_t name_length = (int64_t)xx_dsl2_le16(entry + 0x14);

            /* The stored length counts its own terminating NUL. */
            name_size = (name_length > 0) ? (name_length - 1) : 0;
            if (name_size > directory_size - name_offset) {
                name_size = directory_size - name_offset;
            }
            name = xx_dsl2_make_name(directory + name_offset,
                                     (size_t)name_size, (size_t)index);
        } else {
            name = xx_dsl2_make_name(directory, 0U, (size_t)index);
        }
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + directory_start + position;
        member.header_size = record_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        /* The container has no method field, only this flags word; it is
         * published unchanged and decode does the mapping. */
        member.method = (uint32_t)flags;
        /* DOS time at 0x0e, DOS date at 0x10, packed date-over-time the usual
         * way. */
        member.timestamp = ((uint64_t)xx_dsl2_le16(entry + 0x10) << 16) |
                           (uint64_t)xx_dsl2_le16(entry + 0x0e);
        member.is_folder = false;
        if (!xx_dsl2_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        data_end = data_offset + compressed_size;
        if (data_end > archive_size) archive_size = data_end;

        left -= record_size;
        position += record_size;
    }

    if (stream->count == 0U) goto fail;
    if (archive_size > span) archive_size = span;

    xx_mem_free(directory);
    stream->archive_size = archive_size;
    return stream;

fail:
    xx_mem_free(directory);
    xx_dsl2_stream_free(stream);
    return NULL;
}


/* Bit 4 of the flags word is the only method bit the format defines; the
 * other bits are attributes, not methods, so they are deliberately not
 * treated as an unknown method. */
/* The uncompressed size is attacker controlled and is the allocation size, so
 * it is capped rather than trusted. */

/* Members are either stored verbatim or PKWARE DCL imploded. The flags word
 * from the directory record is carried in member->method unchanged, so a
 * listing shows what the archive actually says. */
static bool xx_dsl2_decode(Abstractformat *self,
                           const xx_dsl2_member *member, uint8_t **out,
                           size_t *out_size, xx_pd_struct *pd) {
    uint8_t *input = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_DSL2_MAX_DECODED) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    input = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!input) return false;
    if (member->compressed_size != 0 &&
        !xx_dsl2_read_at(self, member->data_offset, input,
                         (size_t)member->compressed_size)) {
        xx_mem_free(input);
        return false;
    }
    /* The read above can be large; give the caller a chance to stop between
     * it and the decode. */
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(input);
        return false;
    }

    output = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!output) {
        xx_mem_free(input);
        return false;
    }

    if (member->method & XX_DSL2_FLAG_COMPRESSED) {
        if (!xx_dcl_decode_memory(input, (size_t)member->compressed_size,
                                  output, (size_t)member->uncompressed_size,
                                  &written)) {
            written = 0U;
            xx_mem_free(input);
            xx_mem_free(output);
            return false;
        }
    } else {
        /* A stored member carries no second size, and parse already refused a
         * record whose two sizes disagree; re-checking here keeps decode
         * correct on its own terms rather than on parse's. */
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(input);
            xx_mem_free(output);
            return false;
        }
        xx_mem_copy(output, input, (size_t)member->uncompressed_size);
        written = (size_t)member->uncompressed_size;
    }
    xx_mem_free(input);

    /* A short decode reported as success is the one failure a caller cannot
     * detect, so the decoder must have written exactly what the container
     * promised. */
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(output);
        return false;
    }
    *out = output;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_dsl2_init(xx_dsl2 *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_DSL2;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dsl-install");
    xx_format_set_extension(&archive->format, "d00");
    archive->format.check_is_valid = xx_dsl2_check_is_valid;
    archive->format.handle_base_info = xx_dsl2_handle_base_info;
    archive->format.get_format_size = xx_dsl2_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_dsl2_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_dsl2_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_dsl2_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_dsl2_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_dsl2_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_dsl2_free_archive_records_reading;
    archive->format.destroy = xx_dsl2_vtable_destroy;
}

xx_dsl2 *xx_dsl2_create(xx_io_device *device, int64_t base_address) {
    xx_dsl2 *archive = (xx_dsl2 *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_dsl2_init(archive, device, base_address);
    return archive;
}

void xx_dsl2_destroy(xx_dsl2 *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_dsl2_free(xx_dsl2 *archive) {
    if (!archive) return;
    xx_dsl2_destroy(archive);
    xx_mem_free(archive);
}

static void xx_dsl2_vtable_destroy(Abstractformat *self) {
    xx_dsl2_destroy((xx_dsl2 *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_dsl2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dsl2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_dsl2_parse(self, pd);
    if (!stream) return false;
    xx_dsl2_stream_free(stream);
    return true;
}

bool xx_dsl2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dsl2 *archive = (xx_dsl2 *)self;
    xx_dsl2_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_dsl2_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_dsl2_stream_free(stream);
    return true;
}

int64_t xx_dsl2_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_dsl2_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_dsl2 *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_dsl2_set_record(xx_archive_record *record,
                                 const xx_dsl2_member *member) {
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

static bool xx_dsl2_copy_options(xx_list_s *target,
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

static const xx_var *xx_dsl2_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_dsl2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_dsl2_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_dsl2_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_dsl2_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_dsl2_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_dsl2_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_dsl2_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_dsl2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dsl2_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_dsl2_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dsl2_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_dsl2_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_dsl2_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_dsl2_stream *stream;
    const xx_dsl2_member *member;
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
    stream = (xx_dsl2_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_dsl2_path_safe(member->name)) return false;

    path_option = xx_dsl2_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_dsl2_decode(self, member, &plain, &plain_size, pd);
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
        !xx_dsl2_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_dsl2_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
