/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SEA ARC archives, the original System Enhancement Associates
 * container, also written by PKPAK/PKARC and by PAK.
 *
 * There is no file header and no central directory: the archive is a chain
 * of member records read front to back, each one located by the size of the
 * one before it.
 *
 *   member record, 25 or 29 bytes, then the payload:
 *     0x00  u8   0x1A, the DOS EOF byte, used here as the record marker
 *     0x01  u8   compression method
 *     0x02  char name[13], NUL terminated, the tail zero filled
 *     0x0F  u32 LE compressed size
 *     0x13  u16 LE DOS date
 *     0x15  u16 LE DOS time
 *     0x17  u16 LE CRC-16/ARC of the plaintext
 *     0x1B  u32 LE original size   -- ONLY when method != 1
 *
 *   The header length is a function of the method, not a constant: method 1
 *   (the original "stored") predates the original-size field and so uses a
 *   25 byte header, while every later method uses 29. Budgeting a flat 29
 *   misparses method-1 archives that period tools still list correctly.
 *
 *   end record, 2 bytes: 0x1A 0x00. Method 0 exists only as this marker; it
 *   never carries a name or a payload. The archive ends there, and anything
 *   after it is overlay.
 *
 * Compression methods:
 *     1   stored, old (no original-size field)
 *     2   stored
 *     3   packed        -- 0x90 run-length only
 *     4   squeezed      -- Huffman node table, then 0x90 run-length
 *     5   crunched      -- 12-bit hash-table LZW, no run-length
 *     6   crunched      -- 12-bit hash-table LZW, old hash, + run-length
 *     7   crunched      -- 12-bit hash-table LZW, new hash, + run-length
 *     8   crunched      -- dynamic-width LZW (leading max-bits byte) + RLE
 *     9   squashed      -- dynamic-width 13-bit LZW, no run-length
 *     10  crushed       -- PAK extension
 *     11  distilled     -- PAK extension
 *     0x7f compressed   -- Unix compress LZW, leading flag byte
 *
 * The container carries no self-identifying magic whatsoever: byte 0 is
 * 0x1A and byte 1 is a small integer. That pair occurs constantly inside
 * executables and inside compressed data, so detection rests entirely on
 * walking the whole chain and landing exactly on a 1A 00 end record.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/seaarc/xx_seaarc.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/arcfs/xx_arcfs_lzw.h"
#include "xxfclib/algo/oraclesqueeze/xx_oraclesqueeze.h"

#include <stdio.h>

#define XX_SEAARC_COPY_CHUNK (64 * 1024)

typedef struct xx_seaarc_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_seaarc_member;

typedef struct xx_seaarc_stream_s {
    xx_seaarc_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_seaarc_stream;

static void xx_seaarc_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_seaarc_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_seaarc_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_seaarc_path_safe(const char *name) {
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

static void xx_seaarc_stream_free(void *pointer) {
    xx_seaarc_stream *stream = (xx_seaarc_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_seaarc_add(xx_seaarc_stream *stream,
                          const xx_seaarc_member *member) {
    xx_seaarc_member *grown = (xx_seaarc_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_SEAARC_MARKER 0x1AU
#define XX_SEAARC_NAME_OFFSET 2
#define XX_SEAARC_NAME_SIZE 13
#define XX_SEAARC_HEADER_SIZE_OLD 25
#define XX_SEAARC_HEADER_SIZE 29
#define XX_SEAARC_END_SIZE 2
#define XX_SEAARC_METHOD_END 0U
#define XX_SEAARC_METHOD_STORE_OLD 1U
#define XX_SEAARC_METHOD_STORE 2U
#define XX_SEAARC_METHOD_PACKED 3U
#define XX_SEAARC_METHOD_SQUEEZED 4U
#define XX_SEAARC_METHOD_CRUNCHED1 5U
#define XX_SEAARC_METHOD_CRUNCHED2 6U
#define XX_SEAARC_METHOD_CRUNCHED3 7U
#define XX_SEAARC_METHOD_CRUNCHED4 8U
#define XX_SEAARC_METHOD_SQUASHED 9U
#define XX_SEAARC_METHOD_CRUSHED 10U
#define XX_SEAARC_METHOD_DISTILLED 11U
#define XX_SEAARC_METHOD_COMPRESSED 0x7FU
#define XX_SEAARC_MAX_MEMBERS 65536
#define XX_SEAARC_MAX_DECODED ((int64_t)256 * 1024 * 1024)
#define XX_SEAARC_LZW_MIN_BITS 9U
#define XX_SEAARC_LZW_CRUNCH_MAX_BITS 12U
#define XX_SEAARC_LZW_SQUASH_BITS 13U
#define XX_SEAARC_LZW_MAX_BITS 16U
#define XX_SEAARC_COMPRESS_BITS_MASK 0x1FU
#define XX_SEAARC_COMPRESS_RESERVED_MASK 0x60U

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_seaarc_le32(const uint8_t *data);
static uint16_t xx_seaarc_le16(const uint8_t *data);
static bool xx_seaarc_method_valid(uint8_t method);
static int64_t xx_seaarc_header_size(uint8_t method);
static char *xx_seaarc_make_name(const uint8_t *field);
static xx_seaarc_stream *xx_seaarc_parse(Abstractformat *self, xx_pd_struct *pd);
static uint8_t *xx_seaarc_load(Abstractformat *self, int64_t data_offset, int64_t size);
static bool xx_seaarc_decode(Abstractformat *self, const xx_seaarc_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* Two header lengths, selected by the method byte -- see the doc block. */

/* Container method numbers, stored in member->method unchanged so a listing
 * shows what the archive actually says. */

/* ARC never stored more than a few thousand members; this is a sanity cap on
 * a chain walk whose length is not declared anywhere. */

/* Both sizes are attacker-controlled u32 fields: refuse rather than attempt
 * the allocation. */

static uint32_t xx_seaarc_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint16_t xx_seaarc_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/* Method 0 is the end record only; it is deliberately not "valid" here. */
static bool xx_seaarc_method_valid(uint8_t method) {
    return (method >= XX_SEAARC_METHOD_STORE_OLD &&
            method <= XX_SEAARC_METHOD_DISTILLED) ||
           method == XX_SEAARC_METHOD_COMPRESSED;
}

static int64_t xx_seaarc_header_size(uint8_t method) {
    return method == XX_SEAARC_METHOD_STORE_OLD ? XX_SEAARC_HEADER_SIZE_OLD
                                                : XX_SEAARC_HEADER_SIZE;
}

/* The 13 byte field holds a bare DOS 8.3 name -- the container has no notion
 * of directories, so a separator in it means this is not an ARC header. */
static char *xx_seaarc_make_name(const uint8_t *field) {
    char *name;
    size_t length = 0U;
    size_t index;

    /* A leading space or a leading NUL is what an arbitrary 0x1A <small int>
     * pair in unrelated data most often produces; requiring a printable
     * non-blank first byte is one of the few per-record gates this format
     * offers, so it is stricter than the 0x20..0x7E rule used below. */
    if (field[0] < 0x21U || field[0] > 0x7EU) return NULL;
    while (length < (size_t)XX_SEAARC_NAME_SIZE && field[length] != 0U) {
        ++length;
    }
    for (index = 0U; index < length; ++index) {
        /* ARC is a DOS-only format and stores no high-bit or control bytes
         * in this field. */
        if (field[index] < 0x20U || field[index] > 0x7EU) return NULL;
        if (field[index] == (uint8_t)'/' || field[index] == (uint8_t)'\\' ||
            field[index] == (uint8_t)':') {
            return NULL;
        }
    }
    /* A name may fill all 13 bytes with no terminator: PKPAK wrote the field
     * that way, so an unterminated name is accepted, not rejected. */
    name = (char *)xx_mem_alloc(length + 1U);
    if (!name) return NULL;
    for (index = 0U; index < length; ++index) name[index] = (char)field[index];
    name[length] = '\0';
    return name;
}

static xx_seaarc_stream *xx_seaarc_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_seaarc_stream *stream;
    uint8_t header[XX_SEAARC_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    bool terminated = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The shortest possible archive is one method-1 member with an empty
     * payload plus the end record. */
    if (span < XX_SEAARC_HEADER_SIZE_OLD + XX_SEAARC_END_SIZE) return NULL;

    stream = (xx_seaarc_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (!terminated) {
        xx_seaarc_member member;
        char *name;
        uint8_t method;
        int64_t header_size;
        int64_t data_offset;
        int64_t compressed_size;
        int64_t uncompressed_size;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_SEAARC_MAX_MEMBERS) goto fail;
        if (!xx_seaarc_range_within(span, offset, XX_SEAARC_END_SIZE) ||
            !xx_seaarc_read_at(self, self->base_address + offset, header,
                               (size_t)XX_SEAARC_END_SIZE)) {
            goto fail;
        }
        /* Every record, the end record included, starts with the marker. A
         * chain that drifts off the record boundaries fails here rather than
         * silently resynchronising on the next 0x1A. */
        if (header[0] != XX_SEAARC_MARKER) goto fail;
        method = header[1];
        if (method == XX_SEAARC_METHOD_END) {
            offset += XX_SEAARC_END_SIZE;
            terminated = true;
            break;
        }
        if (!xx_seaarc_method_valid(method)) goto fail;

        header_size = xx_seaarc_header_size(method);
        if (!xx_seaarc_range_within(span, offset, header_size) ||
            !xx_seaarc_read_at(self, self->base_address + offset, header,
                               (size_t)header_size)) {
            goto fail;
        }

        compressed_size = (int64_t)xx_seaarc_le32(header + 15);
        if (method == XX_SEAARC_METHOD_STORE_OLD) {
            /* The old header has no original-size field at all; for a stored
             * member the two lengths are the same by definition. */
            uncompressed_size = compressed_size;
        } else {
            uncompressed_size = (int64_t)xx_seaarc_le32(header + 25);
        }

        data_offset = offset + header_size;
        /* A member whose payload runs past EOF is a rejection, not a
         * truncated-but-listable member. Without this the chain walk would
         * read the end marker out of unrelated trailing bytes. */
        if (!xx_seaarc_range_within(span, data_offset, compressed_size)) {
            goto fail;
        }
        /* The stored methods are the only ones whose two lengths must agree,
         * and making them agree here means decode can copy without having to
         * decide which length to trust. */
        if ((method == XX_SEAARC_METHOD_STORE_OLD ||
             method == XX_SEAARC_METHOD_STORE) &&
            compressed_size != uncompressed_size) {
            goto fail;
        }

        name = xx_seaarc_make_name(header + XX_SEAARC_NAME_OFFSET);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = header_size;
        member.data_offset = self->base_address + data_offset;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = (uint32_t)method;
        /* Date at +0x13, time at +0x15, published as the usual packed dword. */
        member.timestamp = ((uint64_t)xx_seaarc_le16(header + 19) << 16) |
                           (uint64_t)xx_seaarc_le16(header + 21);
        member.is_folder = false;
        if (!xx_seaarc_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }

        offset = data_offset + compressed_size;
    }

    /* Two rules that together are this format's ONLY defence against a false
     * positive, because it has no magic: the chain must end on a real 1A 00
     * end record (reaching EOF or an unrelated byte is not a substitute), and
     * it must have carried at least one member. A bare "1A 00" pair, or a
     * chain that merely happens to run out of file, is not an archive. */
    if (!terminated) goto fail;
    if (stream->count == 0U) goto fail;
    stream->archive_size = offset;
    return stream;

fail:
    xx_seaarc_stream_free(stream);
    return NULL;
}


/* Dynamic-width LZW widths. Methods 8 and 0x7f each begin with one byte
 * declaring the maximum code width; method 9 does not and is fixed at 13. */
/* Unix compress flag byte: low five bits are the width, 0x80 is block mode,
 * and 0x60 is reserved and must be clear. */

/* Read a member's payload into a fresh buffer. */
static uint8_t *xx_seaarc_load(Abstractformat *self, int64_t data_offset,
                               int64_t size) {
    uint8_t *packed;

    if (size < 0 || (uint64_t)size > (uint64_t)SIZE_MAX) return NULL;
    packed = (uint8_t *)xx_mem_alloc(size != 0 ? (size_t)size : 1U);
    if (!packed) return NULL;
    if (size != 0 &&
        !xx_seaarc_read_at(self, data_offset, packed, (size_t)size)) {
        xx_mem_free(packed);
        return NULL;
    }
    return packed;
}

/*
 * Method -> decoder map. The library carries exactly three ARC-family
 * entry points, and each container method is routed to the one that matches
 * its bit stream:
 *
 *   3    -> xx_arcfs_rle90_decode_memory
 *   4    -> xx_oraclesqueeze_decompress_memory (node table + LSB-first
 *           Huffman + the 0x90 run stage, which is precisely ARC squeeze)
 *   8    -> xx_arcfs_lzw_decode_memory, declared width 9..12, RLE90 on
 *   9    -> xx_arcfs_lzw_decode_memory, width 13, RLE90 off
 *   0x7f -> xx_arcfs_lzw_decode_memory, flag-byte width 9..16, RLE90 off
 *
 * Methods 5, 6 and 7 -- ARC's ORIGINAL "crunch" -- have no entry point in
 * the library and are refused below. They are not a variant of the method 8
 * bit stream: their codes are nybble packed, there is no CLEAR code, and
 * init_tab hashes even the 256 atomic codes so a literal's code is not its
 * byte value. Handing them to xx_arcfs_lzw_decode_memory would not fail
 * cleanly, it would emit plausible garbage.
 *
 * Methods 10 (PAK "crushed") and 11 (PAK "distilled") likewise have no entry
 * point in the library and are refused.
 */
static bool xx_seaarc_decode(Abstractformat *self,
                             const xx_seaarc_member *member, uint8_t **out,
                             size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    const uint8_t *codes;
    size_t code_size;
    size_t written = 0U;
    uint8_t declared = 0U;
    uint8_t max_bits;
    bool decoded = false;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    /* Both lengths come straight out of the container. */
    if (member->compressed_size > XX_SEAARC_MAX_DECODED ||
        member->uncompressed_size > XX_SEAARC_MAX_DECODED) {
        return false;
    }

    /* Refuse every method this reader cannot actually decode BEFORE any
     * buffer is touched. Falling through to a stored copy for an ARC method
     * would hand the caller still-compressed bytes dressed up as the file. */
    switch (member->method) {
        case XX_SEAARC_METHOD_STORE_OLD:
        case XX_SEAARC_METHOD_STORE:
        case XX_SEAARC_METHOD_PACKED:
        case XX_SEAARC_METHOD_SQUEEZED:
        case XX_SEAARC_METHOD_CRUNCHED4:
        case XX_SEAARC_METHOD_SQUASHED:
        case XX_SEAARC_METHOD_COMPRESSED:
            break;
        case XX_SEAARC_METHOD_CRUNCHED1:  /* ARC_CRUNCH_OLD  - no decoder */
        case XX_SEAARC_METHOD_CRUNCHED2:  /* ARC_CRUNCH      - no decoder */
        case XX_SEAARC_METHOD_CRUNCHED3:  /* ARC_CRUNCH_HASHNEW - no decoder */
        case XX_SEAARC_METHOD_CRUSHED:    /* PAK_CRUSHED     - no decoder */
        case XX_SEAARC_METHOD_DISTILLED:  /* PAK_DISTILLED   - no decoder */
        default:
            return false;
    }

    if (member->uncompressed_size == 0) {
        /* A real, empty file. No bit stream can turn an empty output into
         * wrong output, so this is safe for every accepted method. */
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }

    packed = xx_seaarc_load(self, member->data_offset,
                            member->compressed_size);
    if (!packed) return false;
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }
    plain = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    codes = packed;
    code_size = (size_t)member->compressed_size;
    max_bits = 0U;

    if (member->method == XX_SEAARC_METHOD_STORE_OLD ||
        member->method == XX_SEAARC_METHOD_STORE) {
        /* parse already required the two lengths to be equal. */
        if (member->compressed_size == member->uncompressed_size) {
            xx_rt_memcpy(plain, packed, (size_t)member->uncompressed_size);
            written = (size_t)member->uncompressed_size;
            decoded = true;
        }
    } else if (member->method == XX_SEAARC_METHOD_PACKED) {
        decoded = xx_arcfs_rle90_decode_memory(
            packed, code_size, plain, (size_t)member->uncompressed_size,
            &written);
    } else if (member->method == XX_SEAARC_METHOD_SQUEEZED) {
        decoded = xx_oraclesqueeze_decompress_memory(
            packed, code_size, plain, (size_t)member->uncompressed_size,
            NULL, NULL);
        if (decoded) written = (size_t)member->uncompressed_size;
    } else {
        if (member->method == XX_SEAARC_METHOD_SQUASHED) {
            max_bits = (uint8_t)XX_SEAARC_LZW_SQUASH_BITS;
        } else {
            /* Methods 8 and 0x7f spend their first payload byte on the
             * width. Consuming it is not optional: leaving it in the bit
             * stream shifts every code by eight bits. */
            if (code_size < 1U) {
                xx_mem_free(plain);
                xx_mem_free(packed);
                return false;
            }
            declared = packed[0];
            codes = packed + 1;
            code_size -= 1U;
            if (member->method == XX_SEAARC_METHOD_COMPRESSED) {
                /* ARC always reserves the CLEAR code here, block-mode flag
                 * present or not, which is exactly what the library decoder
                 * assumes; only the reserved bits and the width are gated. */
                if ((declared & XX_SEAARC_COMPRESS_RESERVED_MASK) != 0U) {
                    xx_mem_free(plain);
                    xx_mem_free(packed);
                    return false;
                }
                max_bits = (uint8_t)(declared & XX_SEAARC_COMPRESS_BITS_MASK);
                if (max_bits < XX_SEAARC_LZW_MIN_BITS ||
                    max_bits > XX_SEAARC_LZW_MAX_BITS) {
                    xx_mem_free(plain);
                    xx_mem_free(packed);
                    return false;
                }
            } else {
                /* Method 8's own range is a plain 9..12, with no flag bits.
                 * A wider declared value is a different method's stream. */
                max_bits = declared;
                if (max_bits < XX_SEAARC_LZW_MIN_BITS ||
                    max_bits > XX_SEAARC_LZW_CRUNCH_MAX_BITS) {
                    xx_mem_free(plain);
                    xx_mem_free(packed);
                    return false;
                }
            }
        }
        /* The run-length stage is part of the method, not a guess: 8 has it,
         * 9 and 0x7f do not. Applying it to a stream that does not want it
         * corrupts every 0x90 byte in the plaintext. */
        decoded = xx_arcfs_lzw_decode_memory(
            codes, code_size, plain, (size_t)member->uncompressed_size,
            max_bits, member->method == XX_SEAARC_METHOD_CRUNCHED4, &written);
    }

    xx_mem_free(packed);
    /* A short decode is the one failure a caller cannot detect once the
     * buffer is handed over, so it is a failure here, never a partial
     * success. */
    if (!decoded || written != (size_t)member->uncompressed_size) {
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

/* ---------------------------------------------------------- lifecycle --- */

void xx_seaarc_init(xx_seaarc *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_SEAARC;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-arc");
    xx_format_set_extension(&archive->format, "arc");
    archive->format.check_is_valid = xx_seaarc_check_is_valid;
    archive->format.handle_base_info = xx_seaarc_handle_base_info;
    archive->format.get_format_size = xx_seaarc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_seaarc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_seaarc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_seaarc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_seaarc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_seaarc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_seaarc_free_archive_records_reading;
    archive->format.destroy = xx_seaarc_vtable_destroy;
}

xx_seaarc *xx_seaarc_create(xx_io_device *device, int64_t base_address) {
    xx_seaarc *archive = (xx_seaarc *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_seaarc_init(archive, device, base_address);
    return archive;
}

void xx_seaarc_destroy(xx_seaarc *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_seaarc_free(xx_seaarc *archive) {
    if (!archive) return;
    xx_seaarc_destroy(archive);
    xx_mem_free(archive);
}

static void xx_seaarc_vtable_destroy(Abstractformat *self) {
    xx_seaarc_destroy((xx_seaarc *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_seaarc_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_seaarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_seaarc_parse(self, pd);
    if (!stream) return false;
    xx_seaarc_stream_free(stream);
    return true;
}

bool xx_seaarc_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_seaarc *archive = (xx_seaarc *)self;
    xx_seaarc_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_seaarc_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_seaarc_stream_free(stream);
    return true;
}

int64_t xx_seaarc_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_seaarc_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_seaarc *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_seaarc_set_record(xx_archive_record *record,
                                 const xx_seaarc_member *member) {
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

static bool xx_seaarc_copy_options(xx_list_s *target,
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

static const xx_var *xx_seaarc_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_seaarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_seaarc_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_seaarc_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_seaarc_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_seaarc_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_seaarc_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_seaarc_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_seaarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_seaarc_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_seaarc_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_seaarc_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_seaarc_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_seaarc_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_seaarc_stream *stream;
    const xx_seaarc_member *member;
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
    stream = (xx_seaarc_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_seaarc_path_safe(member->name)) return false;

    path_option = xx_seaarc_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_seaarc_decode(self, member, &plain, &plain_size, pd);
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
        !xx_seaarc_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_seaarc_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
