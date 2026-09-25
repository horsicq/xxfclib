/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * BWCF distribution sets (.set).
 *
 *   file header, 0x56 bytes:
 *     0x00  "BWCF", 4 bytes
 *     0x04  u8  version, 1 or 2
 *     0x05  description, NUL terminated, to the end of the header
 *
 *   Members follow back to back from 0x56 to the end of the archive. There
 *   is no member count and no terminator: the walk stops when it reaches
 *   EOF, which is why every field below has to be validated - the loop has
 *   no directory telling it when to stop.
 *
 *   name block, version 1, 0x10c bytes:
 *     0x00  name, NUL terminated and NUL padded to the full field
 *
 *   name block, version 2, variable:
 *     0x00  "MFTS", 4 bytes
 *     0x04  u8  0x02
 *     0x05  u8  name length, then that many bytes of name
 *     ....  u8  directory length, then that many bytes of directory
 *   The name comes FIRST and the destination directory SECOND, which is the
 *   reverse of the order they are joined in; either may be empty.
 *
 *   descriptor, 0x11 bytes:
 *     0x00  u16 LE DOS time
 *     0x02  u16 LE DOS date
 *     0x04  i32 LE uncompressed size
 *     0x08  i32 LE block size, covering the 4 byte prefix below
 *     0x0c  u32 LE reserved, must be zero
 *     0x10  u8  method: 1 = LZHUF, 3 = stored
 *
 *   data block, `block size` bytes:
 *     0x00  i32 LE uncompressed size AGAIN
 *     0x04  payload, `block size` - 4 bytes
 *
 * The block's leading repeat of the uncompressed size is the format's spine.
 * It is checked against the descriptor's copy, and it is NOT part of the
 * payload: xx_lzhuf_decode_memory() wants the bytes after it, so the member's
 * data offset is the block offset plus four.
 *
 * "BWCF" is four bytes and turns up by accident; what makes this container
 * identifiable is the agreement of those two independent size copies in every
 * record, plus the zero reserved word and the two-value method field.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/bwcf/xx_bwcf.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzhuf/xx_lzhuf.h"

#include <stdio.h>

#define XX_BWCF_COPY_CHUNK (64 * 1024)

typedef struct xx_bwcf_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_bwcf_member;

typedef struct xx_bwcf_stream_s {
    xx_bwcf_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_bwcf_stream;

static void xx_bwcf_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_bwcf_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_bwcf_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_bwcf_path_safe(const char *name) {
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

static void xx_bwcf_stream_free(void *pointer) {
    xx_bwcf_stream *stream = (xx_bwcf_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_bwcf_add(xx_bwcf_stream *stream,
                          const xx_bwcf_member *member) {
    xx_bwcf_member *grown = (xx_bwcf_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_BWCF_HEADER_SIZE 0x56
#define XX_BWCF_DESCRIPTION_OFFSET 0x05
#define XX_BWCF_V1_NAME_FIELD_SIZE 0x10c
#define XX_BWCF_V2_TAG_SIZE 5
#define XX_BWCF_DESCRIPTOR_SIZE 0x11
#define XX_BWCF_BLOCK_PREFIX_SIZE 4
#define XX_BWCF_VERSION_MIN 1
#define XX_BWCF_VERSION_MAX 2
#define XX_BWCF_METHOD_LZHUF 1U
#define XX_BWCF_METHOD_STORE 3U
#define XX_BWCF_MAX_MEMBERS 200000
#define XX_BWCF_MAX_NAME_SIZE 255
#define XX_BWCF_MAX_UNCOMPRESSED (512 * 1024 * 1024)
#define XX_BWCF_NAME_BUFFER 0x220
#define XX_BWCF_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_bwcf_le16(const uint8_t *data);
static uint32_t xx_bwcf_le32(const uint8_t *data);
static uint64_t xx_bwcf_dos_to_unix(uint16_t dos_date, uint16_t dos_time);
static bool xx_bwcf_append_name(char *buffer, size_t *length, const uint8_t *data, size_t size);
static size_t xx_bwcf_field_length(const uint8_t *field, size_t size);
static xx_bwcf_stream *xx_bwcf_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_bwcf_decode(Abstractformat *self, const xx_bwcf_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Each half of a version 2 name is u8 length prefixed, so 255 is the field's
 * own ceiling rather than a policy limit. */
/* Directory plus name plus a separating NUL, with the version 1 field (which
 * is larger than two version 2 halves) setting the floor. */

static uint16_t xx_bwcf_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_bwcf_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* DOS date/time -> Unix seconds. Written out rather than taken from a helper
 * because there is no CRT here; an out-of-range field yields 0 (unknown)
 * instead of a bogus instant. */
static uint64_t xx_bwcf_dos_to_unix(uint16_t dos_date, uint16_t dos_time) {
    static const int32_t days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int32_t year;
    int32_t month;
    int32_t day;
    int32_t hour;
    int32_t minute;
    int32_t second;
    int32_t cursor;
    int64_t days;

    year = 1980 + (int32_t)((dos_date >> 9) & 0x7f);
    month = (int32_t)((dos_date >> 5) & 0x0f);
    day = (int32_t)(dos_date & 0x1f);
    hour = (int32_t)((dos_time >> 11) & 0x1f);
    minute = (int32_t)((dos_time >> 5) & 0x3f);
    second = (int32_t)((dos_time & 0x1f) * 2);

    if (month < 1 || month > 12) return 0U;
    if (day < 1 || day > 31) return 0U;
    if (hour > 23 || minute > 59 || second > 59) return 0U;

    days = 0;
    for (cursor = 1970; cursor < year; ++cursor) {
        bool leap = ((cursor % 4) == 0 && (cursor % 100) != 0) ||
                    ((cursor % 400) == 0);
        days += leap ? 366 : 365;
    }
    days += days_before_month[month - 1];
    if (month > 2 && (((year % 4) == 0 && (year % 100) != 0) ||
                      ((year % 400) == 0))) {
        days += 1;
    }
    days += day - 1;

    return (uint64_t)(days * 86400 + hour * 3600 + minute * 60 + second);
}

/* Append one half of a member name, converting the DOS separator on the way.
 * Everything here is a rejection rather than a sanitisation: the container
 * states this path as the member's destination, and quietly rewriting it
 * would publish a name the archive does not carry. */
static bool xx_bwcf_append_name(char *buffer, size_t *length,
                                const uint8_t *data, size_t size) {
    size_t cursor;

    for (cursor = 0U; cursor < size; ++cursor) {
        uint8_t byte = data[cursor];
        /* BWCF names are DOS paths, i.e. plain printable ASCII. A byte
         * outside that range means the walk has drifted off a record
         * boundary and is reading payload as a name. */
        if (byte < 0x20U || byte > 0x7EU) return false;
        /* A drive letter would make the path absolute on extraction. */
        if (byte == (uint8_t)':') return false;
        if (*length + 1U >= (size_t)XX_BWCF_NAME_BUFFER) return false;
        buffer[*length] = (byte == (uint8_t)'\\') ? '/' : (char)byte;
        *length += 1U;
    }
    return true;
}

/* Length of the NUL terminated string inside a fixed width field. */
static size_t xx_bwcf_field_length(const uint8_t *field, size_t size) {
    size_t cursor = 0U;

    while (cursor < size && field[cursor] != 0U) ++cursor;
    return cursor;
}

static xx_bwcf_stream *xx_bwcf_parse(Abstractformat *self, xx_pd_struct *pd) {
    static const uint8_t magic[4] = {'B', 'W', 'C', 'F'};
    static const uint8_t v2_tag[4] = {'M', 'F', 'T', 'S'};
    xx_bwcf_stream *stream;
    uint8_t header[XX_BWCF_HEADER_SIZE];
    uint8_t version;
    uint8_t first_description_byte;
    int64_t total;
    int64_t span;
    int64_t offset;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)(XX_BWCF_HEADER_SIZE + XX_BWCF_DESCRIPTOR_SIZE)) {
        return NULL;
    }
    if (!xx_bwcf_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, magic, sizeof(magic)) != 0) return NULL;

    version = header[4];
    if (version < XX_BWCF_VERSION_MIN || version > XX_BWCF_VERSION_MAX) {
        return NULL;
    }

    /* The description is a real, populated string in every BWCF set, so its
     * first byte being printable is part of the whole-file gate: four magic
     * bytes followed by a zero or a binary byte is not this format. Dropping
     * this leaves only "BWCF" plus a version byte guarding the header. */
    first_description_byte = header[XX_BWCF_DESCRIPTION_OFFSET];
    if (first_description_byte < 0x20U || first_description_byte >= 0x7FU) {
        return NULL;
    }

    stream = (xx_bwcf_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_BWCF_HEADER_SIZE;

    while (offset < span) {
        xx_bwcf_member member;
        uint8_t name_field[XX_BWCF_V1_NAME_FIELD_SIZE];
        uint8_t descriptor[XX_BWCF_DESCRIPTOR_SIZE];
        uint8_t prefix[XX_BWCF_BLOCK_PREFIX_SIZE];
        char combined[XX_BWCF_NAME_BUFFER];
        char *name;
        size_t combined_length;
        size_t directory_length;
        size_t file_length;
        uint8_t directory[XX_BWCF_MAX_NAME_SIZE];
        uint8_t file_name[XX_BWCF_MAX_NAME_SIZE];
        int64_t header_offset;
        int64_t uncompressed_size;
        int64_t block_size;
        int64_t repeated_size;
        uint32_t reserved;
        uint8_t method;
        int32_t half;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_BWCF_MAX_MEMBERS) goto fail;

        header_offset = offset;
        directory_length = 0U;
        file_length = 0U;

        if (version == 1) {
            if (!xx_bwcf_range_within(span, offset,
                                      XX_BWCF_V1_NAME_FIELD_SIZE)) {
                goto fail;
            }
            if (!xx_bwcf_read_at(self, self->base_address + offset, name_field,
                                 sizeof(name_field))) {
                goto fail;
            }
            file_length =
                xx_bwcf_field_length(name_field, sizeof(name_field));
            if (file_length > (size_t)XX_BWCF_MAX_NAME_SIZE) goto fail;
            for (half = 0; half < (int32_t)file_length; ++half) {
                file_name[half] = name_field[half];
            }
            offset += XX_BWCF_V1_NAME_FIELD_SIZE;
        } else {
            uint8_t tag[XX_BWCF_V2_TAG_SIZE];

            if (!xx_bwcf_range_within(span, offset, XX_BWCF_V2_TAG_SIZE)) {
                goto fail;
            }
            if (!xx_bwcf_read_at(self, self->base_address + offset, tag,
                                 sizeof(tag))) {
                goto fail;
            }
            /* Every version 2 record restates this tag. It is what keeps the
             * loop honest: the walk has no member count, so a record whose
             * sizes were misread lands the next iteration on data that does
             * not open with "MFTS" and the archive is refused rather than
             * mined for plausible-looking members. */
            if (xx_rt_memcmp(tag, v2_tag, sizeof(v2_tag)) != 0) goto fail;
            if (tag[4] != 0x02U) goto fail;
            offset += XX_BWCF_V2_TAG_SIZE;

            for (half = 0; half < 2; ++half) {
                uint8_t length_byte;
                int64_t length;

                if (!xx_bwcf_range_within(span, offset, 1)) goto fail;
                if (!xx_bwcf_read_at(self, self->base_address + offset,
                                     &length_byte, 1U)) {
                    goto fail;
                }
                offset += 1;
                length = (int64_t)length_byte;
                if (length > (int64_t)XX_BWCF_MAX_NAME_SIZE) goto fail;
                if (length > 0) {
                    if (!xx_bwcf_range_within(span, offset, length)) goto fail;
                    if (!xx_bwcf_read_at(self, self->base_address + offset,
                                         (half == 0) ? file_name : directory,
                                         (size_t)length)) {
                        goto fail;
                    }
                    offset += length;
                }
                /* Name first, directory second - the reverse of the order
                 * they are joined in below. */
                if (half == 0) {
                    file_length = (size_t)length;
                } else {
                    directory_length = (size_t)length;
                }
            }
        }

        if (!xx_bwcf_range_within(span, offset, XX_BWCF_DESCRIPTOR_SIZE)) {
            goto fail;
        }
        if (!xx_bwcf_read_at(self, self->base_address + offset, descriptor,
                             sizeof(descriptor))) {
            goto fail;
        }
        offset += XX_BWCF_DESCRIPTOR_SIZE;

        /* Signed on purpose: a size field with the top bit set is corrupt,
         * not a two-gigabyte quantity. */
        uncompressed_size = (int64_t)(int32_t)xx_bwcf_le32(descriptor + 0x04);
        block_size = (int64_t)(int32_t)xx_bwcf_le32(descriptor + 0x08);
        reserved = xx_bwcf_le32(descriptor + 0x0c);
        method = descriptor[0x10];

        if (uncompressed_size < 0 || block_size < 0) goto fail;
        /* Four bytes that must be zero in every record: the cheapest and
         * most load bearing of the per-record gates. */
        if (reserved != 0U) goto fail;
        if (uncompressed_size > (int64_t)XX_BWCF_MAX_UNCOMPRESSED) goto fail;
        /* The block always carries at least its own size prefix. */
        if (block_size < XX_BWCF_BLOCK_PREFIX_SIZE) goto fail;
        if (!xx_bwcf_range_within(span, offset, block_size)) goto fail;
        /* The format defines exactly two methods; anything else means the
         * descriptor was read at the wrong offset. */
        if (method != (uint8_t)XX_BWCF_METHOD_LZHUF &&
            method != (uint8_t)XX_BWCF_METHOD_STORE) {
            goto fail;
        }

        if (!xx_bwcf_read_at(self, self->base_address + offset, prefix,
                             sizeof(prefix))) {
            goto fail;
        }
        repeated_size = (int64_t)(int32_t)xx_bwcf_le32(prefix);
        /* THE defence against a false positive, and the one a later reader
         * will be tempted to drop: the data block opens with an independent
         * repeat of the descriptor's uncompressed size. Two 32-bit fields
         * 0x11 bytes apart agreeing, in every record, is what separates a
         * real set from a file that merely begins "BWCF". */
        if (repeated_size != uncompressed_size) goto fail;
        /* A stored member's block is exactly the payload plus the prefix, so
         * the block size is a third, derived copy of the same number. */
        if (method == (uint8_t)XX_BWCF_METHOD_STORE &&
            block_size != uncompressed_size + XX_BWCF_BLOCK_PREFIX_SIZE) {
            goto fail;
        }

        combined_length = 0U;
        if (!xx_bwcf_append_name(combined, &combined_length, directory,
                                 directory_length)) {
            goto fail;
        }
        if (!xx_bwcf_append_name(combined, &combined_length, file_name,
                                 file_length)) {
            goto fail;
        }
        combined[combined_length] = '\0';
        /* The directory is load bearing: the container states it as the
         * member's destination, and the same bare names recur in several
         * directories, so dropping it would both rename the member and make
         * distinct members collide. An empty result is a rejection. */
        if (combined_length == 0U) goto fail;
        if (!xx_bwcf_path_safe(combined)) goto fail;
        name = xx_str_dup(combined);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + header_offset;
        member.header_size = offset - header_offset;
        /* Past the block's repeat of the uncompressed size: that prefix is
         * framing, not the first four bytes of the LZHUF stream. */
        member.data_offset =
            self->base_address + offset + XX_BWCF_BLOCK_PREFIX_SIZE;
        member.compressed_size = block_size - XX_BWCF_BLOCK_PREFIX_SIZE;
        member.uncompressed_size = uncompressed_size;
        member.method = (uint32_t)method;
        member.timestamp = xx_bwcf_dos_to_unix(
            xx_bwcf_le16(descriptor + 0x02), xx_bwcf_le16(descriptor + 0x00));
        member.is_folder = false;

        if (!xx_bwcf_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset += block_size;
    }

    if (stream->count == 0U) goto fail;
    stream->archive_size = (offset < span) ? offset : span;
    return stream;

fail:
    xx_bwcf_stream_free(stream);
    return NULL;
}


/* A member's uncompressed size is attacker-controlled; refuse rather than
 * attempt an allocation the container merely claims to need. Parse admits
 * members up to the reference's 512 MiB, so a member between this ceiling
 * and that one lists but refuses to extract - which is the right way round. */

static bool xx_bwcf_decode(Abstractformat *self, const xx_bwcf_member *member,
                           uint8_t **out, size_t *out_size,
                           xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > (int64_t)XX_BWCF_MAX_DECODED ||
        member->compressed_size > (int64_t)XX_BWCF_MAX_DECODED) {
        return false;
    }
    if ((uint64_t)member->uncompressed_size > (uint64_t)SIZE_MAX ||
        (uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) {
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    if (member->method == XX_BWCF_METHOD_STORE) {
        /* Parse already required block size == uncompressed size + 4 for a
         * stored member, so a disagreement here means the two came from
         * different records. Copying the shorter of the two would hand the
         * caller a truncated file that reports success. */
        if (member->compressed_size != member->uncompressed_size) return false;
        plain = (uint8_t *)xx_mem_alloc(
            member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                           : 1U);
        if (!plain) return false;
        if (member->uncompressed_size != 0 &&
            !xx_bwcf_read_at(self, member->data_offset, plain,
                             (size_t)member->uncompressed_size)) {
            xx_mem_free(plain);
            return false;
        }
        if (pd && xx_pd_is_stopped(pd)) {
            xx_mem_free(plain);
            return false;
        }
        *out = plain;
        *out_size = (size_t)member->uncompressed_size;
        return true;
    }

    /* Method 0 and 2 exist in the wild but nothing here knows their shape.
     * Falling through to a stored copy would produce garbage that looks like
     * data, so an unimplemented method is a refusal. */
    if (member->method != XX_BWCF_METHOD_LZHUF) return false;
    if (member->compressed_size <= 0) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return false;
    if (!xx_bwcf_read_at(self, member->data_offset, packed,
                         (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(
        member->uncompressed_size != 0 ? (size_t)member->uncompressed_size
                                       : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* data_offset already skips the block's four byte repeat of the
     * uncompressed size: the codec is a bare LZHUF bit stream and treating
     * that prefix as the first four bytes of it decodes nothing but noise.
     *
     * The stream carries no end symbol, so the requested length is the ONLY
     * stop condition - asking for fewer bytes than the member holds succeeds
     * and silently truncates. The length passed is the container's own, which
     * parse has already matched against the block's second copy. */
    if (!xx_lzhuf_decode_memory(packed, (size_t)member->compressed_size, plain,
                                (size_t)member->uncompressed_size, &written)) {
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    if (written != (size_t)member->uncompressed_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_bwcf_init(xx_bwcf *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_BWCF;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-bwcf");
    xx_format_set_extension(&archive->format, "set");
    archive->format.check_is_valid = xx_bwcf_check_is_valid;
    archive->format.handle_base_info = xx_bwcf_handle_base_info;
    archive->format.get_format_size = xx_bwcf_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_bwcf_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_bwcf_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_bwcf_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_bwcf_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_bwcf_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_bwcf_free_archive_records_reading;
    archive->format.destroy = xx_bwcf_vtable_destroy;
}

xx_bwcf *xx_bwcf_create(xx_io_device *device, int64_t base_address) {
    xx_bwcf *archive = (xx_bwcf *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_bwcf_init(archive, device, base_address);
    return archive;
}

void xx_bwcf_destroy(xx_bwcf *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_bwcf_free(xx_bwcf *archive) {
    if (!archive) return;
    xx_bwcf_destroy(archive);
    xx_mem_free(archive);
}

static void xx_bwcf_vtable_destroy(Abstractformat *self) {
    xx_bwcf_destroy((xx_bwcf *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_bwcf_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_bwcf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_bwcf_parse(self, pd);
    if (!stream) return false;
    xx_bwcf_stream_free(stream);
    return true;
}

bool xx_bwcf_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_bwcf *archive = (xx_bwcf *)self;
    xx_bwcf_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_bwcf_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_bwcf_stream_free(stream);
    return true;
}

int64_t xx_bwcf_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_bwcf_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_bwcf *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_bwcf_set_record(xx_archive_record *record,
                                 const xx_bwcf_member *member) {
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

static bool xx_bwcf_copy_options(xx_list_s *target,
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

static const xx_var *xx_bwcf_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_bwcf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_bwcf_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_bwcf_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_bwcf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_bwcf_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_bwcf_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_bwcf_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_bwcf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_bwcf_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_bwcf_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_bwcf_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_bwcf_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_bwcf_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_bwcf_stream *stream;
    const xx_bwcf_member *member;
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
    stream = (xx_bwcf_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_bwcf_path_safe(member->name)) return false;

    path_option = xx_bwcf_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_bwcf_decode(self, member, &plain, &plain_size, pd);
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
        !xx_bwcf_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_bwcf_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
