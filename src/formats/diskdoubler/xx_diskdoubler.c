/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DiskDoubler compressed files (.dd).
 *
 * DiskDoubler compressed a single Macintosh file in place: the result is not
 * a directory-bearing archive but one header describing the two forks that
 * every Macintosh file has. Each fork is compressed independently, with its
 * own codec byte and its own checksum, which is why this reader publishes two
 * members rather than one.
 *
 * All fields are BIG-endian; the format is a 68k Macintosh one.
 *
 *   file header, 84 bytes, exactly once at the base address:
 *     0x00  u32  magic 0xABCD0054
 *     0x04  u32  data fork plaintext size
 *     0x08  u32  data fork packed size
 *     0x0c  u32  resource fork plaintext size
 *     0x10  u32  resource fork packed size
 *     0x14  u8   data fork method, low 7 bits select the codec
 *     0x15  u8   resource fork method, same encoding
 *     0x16  u8   format generation; the LZW codec needs this byte
 *     0x17  0x2f  Finder info, type/creator, dates -- not load-bearing here
 *     0x30  u16  data fork additive checksum (LZW codec input)
 *     0x32  u16  resource fork additive checksum (LZW codec input)
 *     0x34  u8   second LZW info byte
 *     0x35  u8   padding
 *     0x36  u16  data fork "delta" filter selector; nonzero is unsupported
 *     0x38  u16  resource fork "delta" filter selector; likewise
 *     0x3a  0x28 remaining Finder/HFS metadata
 *     0x52  u16  CRC-16/CCITT over header bytes 0x00..0x51, or zero on very
 *                early files that predate the field
 *
 *   0x54          data fork, "data fork packed size" bytes
 *   0x54+dp       resource fork, "resource fork packed size" bytes
 *
 * Method byte, masked with 0x7f:
 *     0   stored
 *     1   Unix-compress LZW, optionally XOR-masked (xx_diskdoubler_lzw)
 *     6   LZSS in 8 KiB blocks (xx_diskdoubler_adn)
 *     8   Compact Pro LZH, with a 16-byte preamble and DiskDoubler's smaller
 *         block size (xx_compactpro_decode_memory)
 *     9   same as 6
 *     10  Huffman-over-LZ77 in 64 KiB blocks (xx_diskdoubler_ddn)
 *
 * Some writers append a second, identical copy of the 84-byte header after
 * the payload. That duplicate is accepted, and anything else trailing is a
 * rejection.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/diskdoubler/xx_diskdoubler.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/diskdoubler/xx_diskdoubler.h"
#include "xxfclib/algo/compactpro/xx_compactpro.h"
#include <stdio.h>

#define XX_DISKDOUBLER_COPY_CHUNK (64 * 1024)

typedef struct xx_diskdoubler_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_diskdoubler_member;

typedef struct xx_diskdoubler_stream_s {
    xx_diskdoubler_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_diskdoubler_stream;

static void xx_diskdoubler_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_diskdoubler_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_diskdoubler_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_diskdoubler_path_safe(const char *name) {
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

static void xx_diskdoubler_stream_free(void *pointer) {
    xx_diskdoubler_stream *stream = (xx_diskdoubler_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_diskdoubler_add(xx_diskdoubler_stream *stream,
                          const xx_diskdoubler_member *member) {
    xx_diskdoubler_member *grown = (xx_diskdoubler_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_DISKDOUBLER_MAX_MEMBERS 2
#define XX_DISKDOUBLER_MAGIC 0xABCD0054U
#define XX_DISKDOUBLER_CRC_OFFSET 82
#define XX_DISKDOUBLER_OFF_DATA_DELTA 54
#define XX_DISKDOUBLER_OFF_RSRC_DELTA 56
#define XX_DISKDOUBLER_MAX_SIZE ((int64_t)512 * 1024 * 1024)
#define XX_DISKDOUBLER_DATA_NAME "unpacked"
#define XX_DISKDOUBLER_RSRC_NAME "unpacked.rsrc"
#define XX_DISKDOUBLER_HEADER_SIZE 84
#define XX_DISKDOUBLER_OFF_DATA_PLAIN 4
#define XX_DISKDOUBLER_OFF_DATA_PACKED 8
#define XX_DISKDOUBLER_OFF_RSRC_PLAIN 12
#define XX_DISKDOUBLER_OFF_RSRC_PACKED 16
#define XX_DISKDOUBLER_OFF_DATA_METHOD 20
#define XX_DISKDOUBLER_OFF_RSRC_METHOD 21
#define XX_DISKDOUBLER_OFF_INFO1 22
#define XX_DISKDOUBLER_OFF_DATA_CHECKSUM 48
#define XX_DISKDOUBLER_OFF_RSRC_CHECKSUM 50
#define XX_DISKDOUBLER_OFF_INFO2 52
#define XX_DISKDOUBLER_METHOD_STORE 0U
#define XX_DISKDOUBLER_METHOD_LZW 1U
#define XX_DISKDOUBLER_METHOD_ADN_6 6U
#define XX_DISKDOUBLER_METHOD_COMPACT_PRO 8U
#define XX_DISKDOUBLER_METHOD_ADN_9 9U
#define XX_DISKDOUBLER_METHOD_DDN 10U
#define XX_DISKDOUBLER_CPT_PREAMBLE 16
#define XX_DISKDOUBLER_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint32_t xx_diskdoubler_be32(const uint8_t *data);
static uint16_t xx_diskdoubler_be16(const uint8_t *data);
static uint16_t xx_diskdoubler_header_crc(const uint8_t *data, int32_t size);
static bool xx_diskdoubler_method_known(uint8_t method);
static bool xx_diskdoubler_publish(xx_diskdoubler_stream *stream, const char *name, int64_t header_offset, int64_t data_offset, int64_t packed, int64_t plain, uint32_t method);
static xx_diskdoubler_stream *xx_diskdoubler_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_diskdoubler_decode(Abstractformat *self, const xx_diskdoubler_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


/* A DiskDoubler file describes exactly one Macintosh file, so at most two
 * members (data fork, resource fork) can ever come out of it. The named cap
 * exists because the contract asks for one, not because the walk could run
 * away. */

/* The CRC covers the header up to but not including the CRC word itself. */
/* Fork "delta" pre-filters: the format reserves them but no writer is known
 * to have shipped one, and applying the wrong filter silently corrupts. */

/* Matches the reference reader's ceiling: a DiskDoubler file is a single
 * compressed Macintosh document, and a half-gigabyte one is noise. */

/* DiskDoubler compressed a file in place and kept its Macintosh name in the
 * filesystem, not in the header, so the header has no name to publish. The
 * reference reader derives one from the file name on disk; this reader has no
 * device name, so it uses a fixed stem and distinguishes the forks by the
 * suffix the reference uses. */

static uint32_t xx_diskdoubler_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint16_t xx_diskdoubler_be16(const uint8_t *data) {
    return (uint16_t)(((uint32_t)data[0] << 8) | (uint32_t)data[1]);
}

/* CRC-16/CCITT over the header, MSB-first, zero seed, no final xor. This is
 * the format's only real magic beyond four signature bytes, and the one check
 * that keeps a file whose first four bytes happen to be AB CD 00 54 from
 * being claimed. Do not loosen it beyond the documented zero-field case. */
static uint16_t xx_diskdoubler_header_crc(const uint8_t *data, int32_t size) {
    uint32_t crc = 0U;
    int32_t index;
    int32_t bit;

    for (index = 0; index < size; ++index) {
        crc ^= (uint32_t)data[index] << 8;
        for (bit = 0; bit < 8; ++bit) {
            crc = ((crc << 1) ^ ((crc & 0x8000U) ? 0x1021U : 0U)) & 0xffffU;
        }
    }
    return (uint16_t)crc;
}

/* True for the codec numbers this format assigns. Method 8 is included: the
 * reference reader lists it as supported but has no dispatch arm for it, so a
 * method-8 fork lists there and then silently fails to extract. This reader
 * implements it, so accepting it in the parse is honest. */
static bool xx_diskdoubler_method_known(uint8_t method) {
    switch (method & 0x7fU) {
        case 0: case 1: case 6: case 8: case 9: case 10: return true;
        default: return false;
    }
}

static bool xx_diskdoubler_publish(xx_diskdoubler_stream *stream,
                                   const char *name, int64_t header_offset,
                                   int64_t data_offset, int64_t packed,
                                   int64_t plain, uint32_t method) {
    xx_diskdoubler_member member;
    char *copy = xx_str_dup(name);

    if (!copy) return false;
    xx_mem_zero(&member, sizeof(member));
    member.name = copy;
    member.header_offset = header_offset;
    member.header_size = XX_DISKDOUBLER_HEADER_SIZE;
    member.data_offset = data_offset;
    member.compressed_size = packed;
    member.uncompressed_size = plain;
    /* The container's own byte, high bit and all; decode masks it. */
    member.method = method;
    /* The header carries Macintosh creation and modification dates (seconds
     * since 1904-01-01 local time, with no zone recorded). Converting them to
     * the UNIX epoch here would invent a zone the file does not state, so the
     * timestamp is left unset rather than published wrong. */
    member.timestamp = 0U;
    /* A DiskDoubler file is one file; it has no directory entries. */
    member.is_folder = false;
    if (!xx_diskdoubler_add(stream, &member)) {
        xx_str_free(copy);
        return false;
    }
    return true;
}

static xx_diskdoubler_stream *xx_diskdoubler_parse(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    xx_diskdoubler_stream *stream;
    uint8_t header[XX_DISKDOUBLER_HEADER_SIZE];
    uint8_t trailer[XX_DISKDOUBLER_HEADER_SIZE];
    int64_t total;
    int64_t span;
    int64_t data_plain;
    int64_t data_packed;
    int64_t rsrc_plain;
    int64_t rsrc_packed;
    int64_t data_offset;
    int64_t rsrc_offset;
    int64_t payload_end;
    int64_t trailing;
    uint16_t stored_crc;
    uint8_t data_method;
    uint8_t rsrc_method;
    bool want_data;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_DISKDOUBLER_HEADER_SIZE) return NULL;
    if (span > XX_DISKDOUBLER_MAX_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_diskdoubler_read_at(self, self->base_address, header,
                                sizeof(header))) {
        return NULL;
    }
    if (xx_diskdoubler_be32(header) != XX_DISKDOUBLER_MAGIC) return NULL;

    stored_crc = xx_diskdoubler_be16(header + XX_DISKDOUBLER_CRC_OFFSET);
    /* Very early DiskDoubler releases left the CRC field zero. A zero word is
     * therefore "no CRC recorded" rather than "CRC of zero", and skipping the
     * check is the documented behaviour, not a loosening. Every other value
     * must match exactly. */
    if (stored_crc != 0U &&
        xx_diskdoubler_header_crc(header, XX_DISKDOUBLER_CRC_OFFSET) !=
            stored_crc) {
        return NULL;
    }

    data_plain = (int64_t)xx_diskdoubler_be32(header +
                                              XX_DISKDOUBLER_OFF_DATA_PLAIN);
    data_packed = (int64_t)xx_diskdoubler_be32(header +
                                               XX_DISKDOUBLER_OFF_DATA_PACKED);
    rsrc_plain = (int64_t)xx_diskdoubler_be32(header +
                                              XX_DISKDOUBLER_OFF_RSRC_PLAIN);
    rsrc_packed = (int64_t)xx_diskdoubler_be32(header +
                                               XX_DISKDOUBLER_OFF_RSRC_PACKED);
    data_method = header[XX_DISKDOUBLER_OFF_DATA_METHOD];
    rsrc_method = header[XX_DISKDOUBLER_OFF_RSRC_METHOD];

    /* A nonzero delta selector means a pre-filter was applied to the fork
     * before compression. No decoder here undoes one, and decoding without it
     * produces plausible-looking garbage, so the whole file is refused. */
    if (xx_diskdoubler_be16(header + XX_DISKDOUBLER_OFF_DATA_DELTA) != 0U ||
        xx_diskdoubler_be16(header + XX_DISKDOUBLER_OFF_RSRC_DELTA) != 0U) {
        return NULL;
    }

    /* A file with an empty data fork and a nonempty resource fork is a normal
     * Macintosh file (an application, a font suitcase), and the header then
     * says nothing meaningful in the data fork's method byte. In every other
     * case the data fork is published, so its method must be one this format
     * defines. */
    want_data = (data_plain != 0) || (rsrc_plain == 0);
    if (want_data && data_plain != 0 &&
        !xx_diskdoubler_method_known(data_method)) {
        return NULL;
    }
    if (rsrc_plain != 0 && !xx_diskdoubler_method_known(rsrc_method)) {
        return NULL;
    }

    data_offset = XX_DISKDOUBLER_HEADER_SIZE;
    rsrc_offset = data_offset + data_packed;
    payload_end = rsrc_offset + rsrc_packed;
    if (!xx_diskdoubler_range_within(span, data_offset, data_packed) ||
        !xx_diskdoubler_range_within(span, rsrc_offset, rsrc_packed) ||
        !xx_diskdoubler_range_within(span, 0, payload_end)) {
        return NULL;
    }

    /* A stored fork's two lengths are the same number written twice. A
     * mismatch means the method byte is not describing this stream, which is
     * the cheapest way a random file with a valid-looking header gives itself
     * away. */
    if ((data_method & 0x7fU) == 0U && data_packed != data_plain) return NULL;
    if ((rsrc_method & 0x7fU) == 0U && rsrc_packed != rsrc_plain) return NULL;

    /* Nothing may follow the two forks except one exact copy of the header,
     * which some writers append. Accepting arbitrary trailing bytes would
     * turn this format into a prefix matcher for any file starting with the
     * magic. */
    trailing = span - payload_end;
    if (trailing != 0) {
        if (trailing != XX_DISKDOUBLER_HEADER_SIZE) return NULL;
        if (!xx_diskdoubler_read_at(self, self->base_address + payload_end,
                                    trailer, sizeof(trailer))) {
            return NULL;
        }
        if (xx_rt_memcmp(trailer, header, sizeof(header)) != 0) return NULL;
    }

    stream = (xx_diskdoubler_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    /* The two forks become two members, not one.
     *
     * They are separate streams in every way the container cares about: each
     * has its own packed length, its own plaintext length, its own codec byte
     * and its own checksum word, and either may be absent. Concatenating them
     * would need a wrapper the file does not contain -- MacBinary or
     * AppleDouble -- so the reader would have to invent bytes. Publishing
     * them separately also keeps a fork whose codec fails from destroying the
     * other fork's extraction. The reference reader makes the same choice,
     * naming the resource fork with a ".rsrc" suffix, which is mirrored here
     * so listings line up. */
    if (want_data) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        /* When the data fork is empty it is published as a stored zero-length
         * member regardless of what its method byte says, matching the
         * reference: there is no stream for a codec to consume. */
        if (!xx_diskdoubler_publish(
                stream, XX_DISKDOUBLER_DATA_NAME, self->base_address,
                self->base_address + data_offset, data_packed, data_plain,
                data_plain != 0 ? (uint32_t)data_method : 0U)) {
            goto fail;
        }
    }
    if (rsrc_plain != 0) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_diskdoubler_publish(
                stream, XX_DISKDOUBLER_RSRC_NAME, self->base_address,
                self->base_address + rsrc_offset, rsrc_packed, rsrc_plain,
                (uint32_t)rsrc_method)) {
            goto fail;
        }
    }
    if (stream->count == 0U || stream->count > XX_DISKDOUBLER_MAX_MEMBERS) {
        goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_diskdoubler_stream_free(stream);
    return NULL;
}


/* Offsets into the 84-byte file header, named so the decode and the parse
 * cannot drift apart. */

/* The container's own codec numbers, stored in member->method unchanged (high
 * bit included) so a listing shows the byte the file actually holds. */

/* Method 8's payload is the Compact Pro LZH stream behind a 16-byte
 * DiskDoubler-specific preamble that is not part of the coded stream. */

/* Both fork sizes are attacker-controlled 32-bit fields: refuse rather than
 * attempt the allocation. */

/* Extraction of one DiskDoubler fork.
 *
 * The header is re-read here rather than cached in the member struct: the LZW
 * codec needs three header fields (two info bytes and the fork's additive
 * checksum) that the generated member struct has nowhere to carry, and the
 * fork's identity -- data or resource -- decides which checksum to use. */
static bool xx_diskdoubler_decode(Abstractformat *self,
                                  const xx_diskdoubler_member *member,
                                  uint8_t **out, size_t *out_size,
                                  xx_pd_struct *pd) {
    uint8_t header[XX_DISKDOUBLER_HEADER_SIZE];
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t packed_size;
    size_t plain_size;
    size_t written = 0U;
    uint32_t method;
    int64_t data_plain;
    int64_t data_packed;
    int64_t rsrc_plain;
    bool is_resource;
    bool ok = false;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_DISKDOUBLER_MAX_DECODED ||
        member->uncompressed_size > XX_DISKDOUBLER_MAX_DECODED) {
        return false;
    }

    method = member->method & 0x7fU;

    /* A fork the archive says is empty decodes to nothing whatever its codec
     * byte says; there is no stream to feed a decoder. */
    if (member->uncompressed_size == 0) {
        if (member->compressed_size != 0) return false;
        plain = (uint8_t *)xx_mem_alloc(1U);
        if (!plain) return false;
        *out = plain;
        *out_size = 0U;
        return true;
    }
    if (member->compressed_size == 0) return false;

    plain_size = (size_t)member->uncompressed_size;
    packed_size = (size_t)member->compressed_size;

    if (!xx_diskdoubler_read_at(self, member->header_offset, header,
                                sizeof(header))) {
        return false;
    }
    data_plain = (int64_t)xx_diskdoubler_be32(header +
                                              XX_DISKDOUBLER_OFF_DATA_PLAIN);
    data_packed = (int64_t)xx_diskdoubler_be32(header +
                                               XX_DISKDOUBLER_OFF_DATA_PACKED);
    rsrc_plain = (int64_t)xx_diskdoubler_be32(header +
                                              XX_DISKDOUBLER_OFF_RSRC_PLAIN);

    /* Which fork this member is. Normally the offset decides it, but when the
     * data fork is absent (plaintext zero) parse publishes only the resource
     * fork and, if the data fork's packed length is also zero, both forks
     * would start at the same offset. The second clause disambiguates that. */
    is_resource = (member->data_offset !=
                   member->header_offset + XX_DISKDOUBLER_HEADER_SIZE) ||
                  (data_plain == 0 && rsrc_plain != 0);
    /* The recomputed start must be the one parse published, or the header
     * being read here does not belong to this member. */
    if (member->data_offset !=
        member->header_offset + XX_DISKDOUBLER_HEADER_SIZE +
            (is_resource ? data_packed : 0)) {
        return false;
    }
    if ((uint32_t)header[is_resource ? XX_DISKDOUBLER_OFF_RSRC_METHOD
                                     : XX_DISKDOUBLER_OFF_DATA_METHOD] !=
        member->method) {
        return false;
    }

    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed) return false;
    if (!xx_diskdoubler_read_at(self, member->data_offset, packed,
                                packed_size)) {
        xx_mem_free(packed);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    if (method == XX_DISKDOUBLER_METHOD_STORE) {
        /* parse already required packed == plaintext for a stored fork, so
         * this is a restatement rather than a new rule -- but decode must not
         * hand out a short buffer if that check is ever relaxed. */
        if (packed_size != plain_size) {
            xx_mem_free(packed);
            return false;
        }
        *out = packed;
        *out_size = plain_size;
        return true;
    }

    plain = (uint8_t *)xx_mem_alloc(plain_size);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (method == XX_DISKDOUBLER_METHOD_LZW) {
        /* The LZW codec verifies an additive checksum of its own output, so
         * it needs the header's per-fork checksum word plus the two info
         * bytes that tell it whether the .Z header and the plaintext are
         * XOR-masked with 0x5a. */
        uint16_t checksum = xx_diskdoubler_be16(
            header + (is_resource ? XX_DISKDOUBLER_OFF_RSRC_CHECKSUM
                                  : XX_DISKDOUBLER_OFF_DATA_CHECKSUM));
        ok = xx_diskdoubler_lzw_decode_memory(
            packed, packed_size, header[XX_DISKDOUBLER_OFF_INFO1],
            header[XX_DISKDOUBLER_OFF_INFO2], checksum, plain, plain_size,
            &written);
    } else if (method == XX_DISKDOUBLER_METHOD_ADN_6 ||
               method == XX_DISKDOUBLER_METHOD_ADN_9) {
        /* 6 and 9 differ only in what the compressor was willing to emit; the
         * stream syntax is identical, so one entry point serves both. */
        ok = xx_diskdoubler_adn_decode_memory(packed, packed_size, plain,
                                              plain_size, &written);
    } else if (method == XX_DISKDOUBLER_METHOD_DDN) {
        ok = xx_diskdoubler_ddn_decode_memory(packed, packed_size, plain,
                                              plain_size, &written);
    } else if (method == XX_DISKDOUBLER_METHOD_COMPACT_PRO) {
        /* Method 8 is Compact Pro's LZH codec verbatim, which is why the
         * diskdoubler module does not carry a second copy of it. Two things
         * differ from a .cpt member: a 16-byte preamble precedes the coded
         * stream, and the LZH block size is DiskDoubler's 0xfff0 rather than
         * Compact Pro's 0x1fff0. The block size is why this calls the generic
         * xx_compactpro_decode_memory rather than the _lzh_ wrapper, which
         * hardcodes the .cpt size. */
        if (packed_size > (size_t)XX_DISKDOUBLER_CPT_PREAMBLE) {
            ok = xx_compactpro_decode_memory(
                packed + XX_DISKDOUBLER_CPT_PREAMBLE,
                packed_size - (size_t)XX_DISKDOUBLER_CPT_PREAMBLE, true,
                (uint32_t)XX_COMPACTPRO_DD_BLOCK_SIZE, plain, plain_size,
                &written);
        }
    } else {
        /* A codec number the format defines but this reader does not
         * implement. Returning false is the point: falling through to a copy
         * would hand the caller compressed bytes that look like data. */
        ok = false;
    }

    xx_mem_free(packed);
    if (pd && xx_pd_is_stopped(pd)) ok = false;
    /* Every DiskDoubler codec knows its exact output length up front, so a
     * short decode is a corrupt member, never a partial success. */
    if (!ok || written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = plain_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_diskdoubler_init(xx_diskdoubler *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_FILE_TYPE_DISKDOUBLER;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-diskdoubler");
    xx_format_set_extension(&archive->format, "dd");
    archive->format.check_is_valid = xx_diskdoubler_check_is_valid;
    archive->format.handle_base_info = xx_diskdoubler_handle_base_info;
    archive->format.get_format_size = xx_diskdoubler_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_diskdoubler_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_diskdoubler_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_diskdoubler_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_diskdoubler_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_diskdoubler_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_diskdoubler_free_archive_records_reading;
    archive->format.destroy = xx_diskdoubler_vtable_destroy;
}

xx_diskdoubler *xx_diskdoubler_create(xx_io_device *device, int64_t base_address) {
    xx_diskdoubler *archive = (xx_diskdoubler *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_diskdoubler_init(archive, device, base_address);
    return archive;
}

void xx_diskdoubler_destroy(xx_diskdoubler *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_diskdoubler_free(xx_diskdoubler *archive) {
    if (!archive) return;
    xx_diskdoubler_destroy(archive);
    xx_mem_free(archive);
}

static void xx_diskdoubler_vtable_destroy(Abstractformat *self) {
    xx_diskdoubler_destroy((xx_diskdoubler *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_diskdoubler_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_diskdoubler_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_diskdoubler_parse(self, pd);
    if (!stream) return false;
    xx_diskdoubler_stream_free(stream);
    return true;
}

bool xx_diskdoubler_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_diskdoubler *archive = (xx_diskdoubler *)self;
    xx_diskdoubler_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_diskdoubler_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_diskdoubler_stream_free(stream);
    return true;
}

int64_t xx_diskdoubler_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_diskdoubler_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_diskdoubler *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_diskdoubler_set_record(xx_archive_record *record,
                                 const xx_diskdoubler_member *member) {
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

static bool xx_diskdoubler_copy_options(xx_list_s *target,
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

static const xx_var *xx_diskdoubler_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_diskdoubler_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_diskdoubler_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_diskdoubler_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_diskdoubler_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_diskdoubler_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_diskdoubler_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_diskdoubler_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_diskdoubler_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_diskdoubler_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_diskdoubler_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_diskdoubler_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_diskdoubler_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_diskdoubler_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_diskdoubler_stream *stream;
    const xx_diskdoubler_member *member;
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
    stream = (xx_diskdoubler_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_diskdoubler_path_safe(member->name)) return false;

    path_option = xx_diskdoubler_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_diskdoubler_decode(self, member, &plain, &plain_size, pd);
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
        !xx_diskdoubler_decode(self, member, &plain, &plain_size, pd)) {
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

void xx_diskdoubler_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
