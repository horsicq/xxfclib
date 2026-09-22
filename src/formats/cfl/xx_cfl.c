/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CFL (version 3) archives.
 *
 * Header, 12 bytes at the base address:
 *
 *   0x00  "CFL3"
 *   0x04  i32 LE directory offset, from the base address
 *   0x08  i32 LE directory size, the INFLATED size of the directory
 *
 * The directory lives in a block, whose 8-byte header is:
 *
 *   0x00  u32 LE block method, 0 = stored, 1 = zlib
 *   0x04  i32 LE block size, the payload length that follows the header
 *
 * For a stored block the payload is the directory itself and the block size
 * must equal the directory size from the file header. For a zlib block the
 * payload opens with a repeated i32 LE inflated size -- which must also equal
 * the directory size -- and the remaining (block size - 4) bytes are a zlib
 * stream: a two-byte RFC 1950 header and raw DEFLATE, with no Adler-32
 * trailer, because the block size already delimits the stream exactly.
 *
 * The inflated directory is a run of records, each 14 bytes plus a name:
 *
 *   0x00  i32 LE uncompressed size
 *   0x04  i32 LE data offset, from the base address
 *   0x08  u16 LE method, 0 = stored, 1 = zlib
 *   0x0A  u16 unused
 *   0x0C  u16 LE name length, in bytes, no terminator
 *   0x0E  the name, name length bytes
 *
 * A record whose size is negative, whose data offset points inside the
 * 12-byte header, or whose name length is zero ends the walk rather than
 * failing the archive: that is how the padding some writers leave after the
 * last record is tolerated.
 *
 * A stored member's data is the raw bytes at its data offset. A zlib member's
 * data offset points at a second, different 8-byte block header:
 *
 *   0x00  i32 LE block size
 *   0x04  i32 LE inflated size, which must equal the record's size
 *
 * followed by (block size - 4) bytes of zlib stream. There is no method field
 * here -- the method comes from the directory record -- so the two block
 * headers are NOT the same structure despite both being eight bytes.
 *
 * The member's published extent skips the two-byte RFC 1950 header, so what
 * this reader hands the decoder is raw DEFLATE: the stream carries no
 * Adler-32, and a zlib decoder that expects one consumes the last four
 * DEFLATE bytes as a checksum and decodes every member short.
 *
 * The magic is four bytes, which is thin on its own. What actually carries
 * the format is that the directory block must decode to exactly the size the
 * file header announced, and that every record in it must then validate.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/cfl/xx_cfl.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include <stdio.h>

#define XX_CFL_COPY_CHUNK (64 * 1024)

typedef struct xx_cfl_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_cfl_member;

typedef struct xx_cfl_stream_s {
    xx_cfl_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_cfl_stream;

static void xx_cfl_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_cfl_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_cfl_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_cfl_path_safe(const char *name) {
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

static void xx_cfl_stream_free(void *pointer) {
    xx_cfl_stream *stream = (xx_cfl_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_cfl_add(xx_cfl_stream *stream,
                          const xx_cfl_member *member) {
    xx_cfl_member *grown = (xx_cfl_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_CFL_HEADER_SIZE 12
#define XX_CFL_ENTRY_SIZE 14
#define XX_CFL_BLOCK_HEADER_SIZE 8
#define XX_CFL_ZLIB_HEADER_SIZE 2
#define XX_CFL_METHOD_STORED 0
#define XX_CFL_METHOD_ZLIB 1
#define XX_CFL_MAX_DIRECTORY 0x20000000
#define XX_CFL_MAX_MEMBERS 1000000
#define XX_CFL_MAX_NAME 4096
#define XX_CFL_MAX_DECODED (256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_cfl_le16(const uint8_t *data);
static uint32_t xx_cfl_le32(const uint8_t *data);
static int64_t xx_cfl_i32(const uint8_t *data);
static uint8_t *xx_cfl_read_block(Abstractformat *self, int64_t span, int64_t offset, int64_t expanded, int64_t *block_size, xx_pd_struct *pd);
static xx_cfl_stream *xx_cfl_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_cfl_decode(Abstractformat *self, const xx_cfl_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* The RFC 1950 header the archive stores in front of each DEFLATE stream. */
/* Name lengths are a u16, so this only refuses the absurd ones. */

static const uint8_t xx_cfl_signature[4] = {'C', 'F', 'L', '3'};

static uint16_t xx_cfl_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_cfl_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Every size and offset in this format is a SIGNED 32-bit value in the
 * reference, and the reference then tests it for being negative. Sign
 * extending here keeps that test meaningful: a field with its high bit set
 * ends the directory walk instead of becoming a four-billion-byte extent. */
static int64_t xx_cfl_i32(const uint8_t *data) {
    return (int64_t)(int32_t)xx_cfl_le32(data);
}

/* The directory's own container. Returns the inflated bytes (exactly
 * `expanded` of them) and reports how many bytes the block occupied in the
 * file, which is where the archive's own extent starts from. */
static uint8_t *xx_cfl_read_block(Abstractformat *self, int64_t span,
                                  int64_t offset, int64_t expanded,
                                  int64_t *block_size, xx_pd_struct *pd) {
    uint8_t head[XX_CFL_BLOCK_HEADER_SIZE];
    uint8_t size_field[4];
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    uint32_t method;
    int64_t payload_size;
    int64_t packed_size;
    size_t written = 0U;

    if (expanded <= 0 || expanded > XX_CFL_MAX_DIRECTORY) return NULL;
    if (!xx_cfl_range_within(span, offset, (int64_t)sizeof(head))) return NULL;
    if (!xx_cfl_read_at(self, self->base_address + offset, head,
                        sizeof(head))) {
        return NULL;
    }
    method = xx_cfl_le32(head);
    payload_size = xx_cfl_i32(head + 4);

    plain = (uint8_t *)xx_mem_alloc((size_t)expanded);
    if (!plain) return NULL;

    if (method == (uint32_t)XX_CFL_METHOD_STORED) {
        /* The block header and the file header each state the directory's
         * length; a file where the two disagree is not a CFL3 archive, and
         * this is the cheapest of the format's structural agreements to
         * check. */
        if (payload_size != expanded) {
            xx_mem_free(plain);
            return NULL;
        }
        if (!xx_cfl_range_within(span, offset + XX_CFL_BLOCK_HEADER_SIZE,
                                 expanded) ||
            !xx_cfl_read_at(self,
                            self->base_address + offset +
                                XX_CFL_BLOCK_HEADER_SIZE,
                            plain, (size_t)expanded)) {
            xx_mem_free(plain);
            return NULL;
        }
        *block_size = XX_CFL_BLOCK_HEADER_SIZE + expanded;
        return plain;
    }
    /* Method 2 and above are not defined by the format; treating an unknown
     * one as stored would publish compressed bytes as a directory. */
    if (method != (uint32_t)XX_CFL_METHOD_ZLIB) {
        xx_mem_free(plain);
        return NULL;
    }
    /* The payload length counts the repeated inflated size that opens it, so
     * four bytes is the minimum a well-formed block can claim. */
    if (payload_size < 4 || payload_size > XX_CFL_MAX_DIRECTORY ||
        !xx_cfl_range_within(span, offset + XX_CFL_BLOCK_HEADER_SIZE, 4) ||
        !xx_cfl_read_at(self,
                        self->base_address + offset + XX_CFL_BLOCK_HEADER_SIZE,
                        size_field, sizeof(size_field))) {
        xx_mem_free(plain);
        return NULL;
    }
    /* The second copy of the inflated size. The file header and the block
     * must agree, which is the strongest thing standing between a random
     * file and a decode attempt. */
    if (xx_cfl_i32(size_field) != expanded) {
        xx_mem_free(plain);
        return NULL;
    }
    packed_size = payload_size - 4;
    if (packed_size < XX_CFL_ZLIB_HEADER_SIZE ||
        !xx_cfl_range_within(span, offset + XX_CFL_BLOCK_HEADER_SIZE + 4,
                             packed_size)) {
        xx_mem_free(plain);
        return NULL;
    }
    packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
    if (!packed) {
        xx_mem_free(plain);
        return NULL;
    }
    if (!xx_cfl_read_at(self,
                        self->base_address + offset +
                            XX_CFL_BLOCK_HEADER_SIZE + 4,
                        packed, (size_t)packed_size) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return NULL;
    }
    /* The directory block keeps its two-byte RFC 1950 header, so it is a zlib
     * stream and is decoded as one. Its Adler-32 trailer is absent -- the
     * block size cuts the stream at the last DEFLATE byte -- which this
     * decoder tolerates, and a short decode is caught by the length test. */
    if (!xx_zlib_stream_decode_memory(packed, (size_t)packed_size, plain,
                                      (size_t)expanded, &written) ||
        written != (size_t)expanded) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return NULL;
    }
    xx_mem_free(packed);
    *block_size = XX_CFL_BLOCK_HEADER_SIZE + 4 + packed_size;
    return plain;
}

static xx_cfl_stream *xx_cfl_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_cfl_stream *stream = NULL;
    uint8_t *directory = NULL;
    uint8_t header[XX_CFL_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t directory_offset;
    int64_t directory_size;
    int64_t directory_block_size = 0;
    int64_t position;
    int64_t archive_end;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_CFL_HEADER_SIZE) return NULL;
    if (!xx_cfl_read_at(self, self->base_address, header, sizeof(header))) {
        return NULL;
    }
    if (xx_rt_memcmp(header, xx_cfl_signature, sizeof(xx_cfl_signature)) != 0) {
        return NULL;
    }

    directory_offset = xx_cfl_i32(header + 4);
    directory_size = xx_cfl_i32(header + 8);
    /* The directory cannot start inside the header that announces it. */
    if (directory_offset < XX_CFL_HEADER_SIZE) return NULL;
    /* An empty directory is not a CFL3 archive: the format has no way to
     * express a zero-member one, and the cap bounds the inflate below. */
    if (directory_size <= 0 || directory_size >= XX_CFL_MAX_DIRECTORY) {
        return NULL;
    }

    directory = xx_cfl_read_block(self, span, directory_offset, directory_size,
                                  &directory_block_size, pd);
    if (!directory) return NULL;

    stream = (xx_cfl_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) {
        xx_mem_free(directory);
        return NULL;
    }
    xx_mem_zero(stream, sizeof(*stream));

    position = 0;
    archive_end = directory_offset + directory_block_size;

    while ((position + XX_CFL_ENTRY_SIZE) <= directory_size) {
        const uint8_t *entry = directory + position;
        xx_cfl_member member;
        char *name;
        int64_t size;
        int64_t data_offset;
        int64_t name_length;
        int64_t index;
        uint16_t method;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_CFL_MAX_MEMBERS) break;

        size = xx_cfl_i32(entry);
        data_offset = xx_cfl_i32(entry + 4);
        method = xx_cfl_le16(entry + 8);
        name_length = (int64_t)xx_cfl_le16(entry + 12);

        /* Not a rejection: a record that fails any of these is where the
         * directory stops, which is how the padding some writers leave after
         * the last record is tolerated. Anything past this point in the
         * record IS a rejection, because a record that started well and then
         * contradicted itself is a damaged archive, not padding. */
        if (size < 0 || data_offset < XX_CFL_HEADER_SIZE ||
            name_length == 0) {
            break;
        }
        if ((position + XX_CFL_ENTRY_SIZE + name_length) > directory_size) {
            break;
        }
        if (name_length > XX_CFL_MAX_NAME) goto fail;

        /* The reference decodes the name as Latin-1 and so accepts any byte.
         * This reader is stricter on purpose: the name becomes a path on
         * extraction, and together with the record's other fields it is the
         * only thing distinguishing a directory from an arbitrary run of
         * bytes that happened to inflate. */
        for (index = 0; index < name_length; ++index) {
            if (entry[XX_CFL_ENTRY_SIZE + index] < 0x20U ||
                entry[XX_CFL_ENTRY_SIZE + index] > 0x7EU) {
                goto fail;
            }
        }

        /* xx_str_free is the allocator's free, so a plain block is fine. */
        name = (char *)xx_mem_alloc((size_t)name_length + 1U);
        if (!name) goto fail;
        for (index = 0; index < name_length; ++index) {
            name[index] = (char)entry[XX_CFL_ENTRY_SIZE + index];
        }
        name[name_length] = '\0';

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        /* The record's position inside the INFLATED directory. The directory
         * may be compressed, so a record has no device coordinate at all;
         * this is a description of where it sits, never something to seek
         * to. */
        member.header_offset = position;
        member.header_size = XX_CFL_ENTRY_SIZE + name_length;
        member.uncompressed_size = size;
        member.method = (uint32_t)method;

        position += XX_CFL_ENTRY_SIZE + name_length;

        if (size == 0) {
            /* No data block exists, so the record's offset is never read;
             * clamp it into the device anyway, because a published member
             * must not advertise an extent the source cannot answer for. */
            member.data_offset =
                self->base_address +
                (data_offset < span ? data_offset : span);
            member.compressed_size = 0;
            member.method = (uint32_t)XX_CFL_METHOD_STORED;
        } else if (method == (uint16_t)XX_CFL_METHOD_STORED) {
            if (!xx_cfl_range_within(span, data_offset, size)) {
                xx_str_free(name);
                goto fail;
            }
            member.data_offset = self->base_address + data_offset;
            member.compressed_size = size;
            if (data_offset + size > archive_end) {
                archive_end = data_offset + size;
            }
        } else if (method == (uint16_t)XX_CFL_METHOD_ZLIB) {
            uint8_t block[XX_CFL_BLOCK_HEADER_SIZE];
            uint8_t zlib_header[XX_CFL_ZLIB_HEADER_SIZE];
            int64_t block_size;
            int64_t expanded;
            int64_t packed_size;

            /* A member's block header is NOT the directory's: it has no
             * method field, because the method is the record's. */
            if (!xx_cfl_range_within(span, data_offset,
                                     (int64_t)sizeof(block)) ||
                !xx_cfl_read_at(self, self->base_address + data_offset, block,
                                sizeof(block))) {
                xx_str_free(name);
                goto fail;
            }
            block_size = xx_cfl_i32(block);
            expanded = xx_cfl_i32(block + 4);
            /* The size the record promised and the size the block promises
             * have to be the same number. */
            if (block_size < 4 || expanded != size) {
                xx_str_free(name);
                goto fail;
            }
            packed_size = block_size - 4;
            if (packed_size < XX_CFL_ZLIB_HEADER_SIZE ||
                !xx_cfl_range_within(span,
                                     data_offset + XX_CFL_BLOCK_HEADER_SIZE,
                                     packed_size)) {
                xx_str_free(name);
                goto fail;
            }
            if (!xx_cfl_read_at(self,
                                self->base_address + data_offset +
                                    XX_CFL_BLOCK_HEADER_SIZE,
                                zlib_header, sizeof(zlib_header))) {
                xx_str_free(name);
                goto fail;
            }
            /* The shared check, not an open-coded one: method, window size,
             * the multiple-of-31 rule and the preset-dictionary flag. */
            if (!xx_zlib_stream_header_is_valid(zlib_header,
                                                sizeof(zlib_header))) {
                xx_str_free(name);
                goto fail;
            }
            /* THE MEMBER STREAM CARRIES NO ADLER-32: two RFC 1950 header
             * bytes, raw DEFLATE, and nothing else. Publishing the extent
             * from the header would make a zlib decoder claim the last four
             * DEFLATE bytes as the checksum footer and decode every member
             * short, so the published extent is the raw DEFLATE alone. */
            member.data_offset = self->base_address + data_offset +
                                 XX_CFL_BLOCK_HEADER_SIZE +
                                 XX_CFL_ZLIB_HEADER_SIZE;
            member.compressed_size = packed_size - XX_CFL_ZLIB_HEADER_SIZE;
            if (data_offset + XX_CFL_BLOCK_HEADER_SIZE + packed_size >
                archive_end) {
                archive_end =
                    data_offset + XX_CFL_BLOCK_HEADER_SIZE + packed_size;
            }
        } else {
            /* A method the format does not define. */
            xx_str_free(name);
            goto fail;
        }

        if (!xx_cfl_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
    }

    /* A header and a directory that held no usable record describe nothing;
     * accepting that would make any file whose first four bytes are "CFL3" an
     * archive with no members. */
    if (stream->count == 0U) goto fail;

    stream->archive_size = archive_end < span ? archive_end : span;

    xx_mem_free(directory);
    return stream;

fail:
    xx_mem_free(directory);
    xx_cfl_stream_free(stream);
    return NULL;
}


/* An attacker-controlled length from the container, so it is capped before it
 * becomes an allocation. */

static bool xx_cfl_decode(Abstractformat *self, const xx_cfl_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;

    *out = NULL;
    *out_size = 0U;
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    if (member->compressed_size < 0 ||
        member->compressed_size > (int64_t)XX_CFL_MAX_DECODED) {
        return false;
    }
    if (member->uncompressed_size < 0 ||
        member->uncompressed_size > (int64_t)XX_CFL_MAX_DECODED) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc(
        member->compressed_size != 0 ? (size_t)member->compressed_size : 1U);
    if (!packed) return false;
    if (member->compressed_size != 0 &&
        !xx_cfl_read_at(self, member->data_offset, packed,
                        (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == (uint32_t)XX_CFL_METHOD_STORED) {
        /* Parse publishes a stored member with both sizes equal; a disagreement
         * here would mean handing the caller fewer bytes than it was told to
         * expect. */
        if (member->compressed_size != member->uncompressed_size) {
            xx_mem_free(packed);
            return false;
        }
        *out = packed;
        *out_size = (size_t)member->compressed_size;
        return true;
    }
    if (member->method != (uint32_t)XX_CFL_METHOD_ZLIB) {
        /* A method the format may define but this reader does not implement.
         * Falling back to a stored copy would return compressed bytes that
         * the caller has no way to recognise as wrong. */
        xx_mem_free(packed);
        return false;
    }

    plain = (uint8_t *)xx_mem_alloc(member->uncompressed_size != 0
                                        ? (size_t)member->uncompressed_size
                                        : 1U);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }
    /* Raw DEFLATE, not zlib: parse validated the two RFC 1950 header bytes and
     * then published the extent that starts after them, because the archive
     * stores no Adler-32 trailer for a zlib decoder to find. */
    if (!xx_deflate_decompress_memory(packed, (size_t)member->compressed_size,
                                      plain, (size_t)member->uncompressed_size,
                                      &written, false) ||
        written != (size_t)member->uncompressed_size) {
        /* Short is a failure, never a success with fewer bytes: the container
         * states the exact decoded length, so anything else is corruption. */
        xx_mem_free(plain);
        xx_mem_free(packed);
        return false;
    }
    xx_mem_free(packed);
    *out = plain;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_cfl_init(xx_cfl *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_CFL;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-cfl");
    xx_format_set_extension(&archive->format, "dat");
    archive->format.check_is_valid = xx_cfl_check_is_valid;
    archive->format.handle_base_info = xx_cfl_handle_base_info;
    archive->format.get_format_size = xx_cfl_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_cfl_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_cfl_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_cfl_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_cfl_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_cfl_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_cfl_free_archive_records_reading;
    archive->format.destroy = xx_cfl_vtable_destroy;
}

xx_cfl *xx_cfl_create(xx_io_device *device, int64_t base_address) {
    xx_cfl *archive = (xx_cfl *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_cfl_init(archive, device, base_address);
    return archive;
}

void xx_cfl_destroy(xx_cfl *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_cfl_free(xx_cfl *archive) {
    if (!archive) return;
    xx_cfl_destroy(archive);
    xx_mem_free(archive);
}

static void xx_cfl_vtable_destroy(Abstractformat *self) {
    xx_cfl_destroy((xx_cfl *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_cfl_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_cfl_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_cfl_parse(self, pd);
    if (!stream) return false;
    xx_cfl_stream_free(stream);
    return true;
}

bool xx_cfl_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_cfl *archive = (xx_cfl *)self;
    xx_cfl_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_cfl_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_cfl_stream_free(stream);
    return true;
}

int64_t xx_cfl_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_cfl_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_cfl *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_cfl_set_record(xx_archive_record *record,
                                 const xx_cfl_member *member) {
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

static bool xx_cfl_copy_options(xx_list_s *target,
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

static const xx_var *xx_cfl_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_cfl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_cfl_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_cfl_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_cfl_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_cfl_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_cfl_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_cfl_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_cfl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_cfl_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_cfl_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_cfl_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_cfl_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_cfl_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_cfl_stream *stream;
    const xx_cfl_member *member;
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
    stream = (xx_cfl_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_cfl_path_safe(member->name)) return false;

    path_option = xx_cfl_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_cfl_decode(self, member, &plain, &plain_size, pd);
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
        !xx_cfl_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_cfl_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
