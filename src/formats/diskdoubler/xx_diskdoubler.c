/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DiskDoubler: single compressed files (.dd) and the two archive forms,
 * DDA2 and DDAR, that bundle such files with a folder tree.
 *
 * All fields are BIG-endian; the format is a 68k Macintosh one. Every header
 * CRC below is CRC-16/CCITT (polynomial 0x1021, MSB-first, zero seed, no
 * final xor) over the header bytes that precede the stored CRC word.
 *
 * 1. Single compressed file. DiskDoubler compressed one Macintosh file in
 *    place: one header describing the two forks every Macintosh file has.
 *    Each fork is compressed independently, with its own codec byte and its
 *    own checksum, which is why a file yields two members rather than one.
 *
 *   file header, 84 bytes:
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
 *     0x52  u16  CRC over header bytes 0x00..0x51, or zero on very early
 *                files that predate the field
 *
 *   0x54          data fork, "data fork packed size" bytes
 *   0x54+dp       resource fork, "resource fork packed size" bytes
 *
 *   A standalone file may be followed by one identical copy of its 84-byte
 *   header, which some writers append; anything else trailing is refused.
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
 * 2. DDA2 archive (DiskDoubler's "combine" archive, usually a .sea).
 *
 *   archive header, 62 bytes:
 *     0x00  "DDA2"
 *     0x04  u16  62, the header's own size
 *     0x1c  u32  sum of all plaintext fork sizes
 *     0x24  u32  archive size, end record and footer included
 *     0x28  u32  offset of the end record
 *     0x3c  u16  CRC over 0x00..0x3b
 *   then a chain of records, each starting with "DDA2" and a u16 type:
 *     type 0xBBBB   end record, 6 bytes
 *     type & 0x8000 folder, 88-byte header
 *     type & 0x4000 compressed file: 56-byte header, then a complete single
 *                   compressed file (section 1) as the record's body
 *     otherwise     stored file, 90-byte header, then the raw data fork and
 *                   the raw resource fork. A record of this type whose
 *                   90-byte header does not check out is read as the 56-byte
 *                   compressed form instead, which is what The Unarchiver
 *                   does with every non-folder record.
 *   common record header fields:
 *     0x06  u8   name length; the name is at 0x07, at most 31 bytes are kept
 *     0x26  u32  id of the folder the record lives in; 0 or 1 means none
 *     0x2a  u32  record size, header included; the next record follows it
 *     size-2 u16 CRC over the header bytes before it
 *   folder header only:
 *     0x3e  u32  the folder's own id, which its children name at 0x26
 *   stored-file header only:
 *     0x4e  u32  data fork size
 *     0x52  u32  resource fork size
 *     0x56  u8   XOR of every data fork byte
 *     0x57  u8   XOR of every resource fork byte
 *   after the end record, a footer: "\xBE\xCD", u32 index length, u16 CRC of
 *   the index, 8 reserved bytes, then the index (folder ids with the top bit
 *   set, each followed by its record offset, and plain record offsets).
 *
 *   The first folder, whose own parent id is 1, is the archive itself (it
 *   carries the archive's name) and is not extracted; its children form the
 *   top of the output. A record naming a folder id that no earlier folder
 *   defined is placed at the top as well.
 *
 * 3. DDAR archive (older DiskDoubler archives).
 *
 *   archive header, 78 bytes: "DDAR", CRC over 0x00..0x4b at 0x4c. Then
 *   records until the end of the data, each a 124-byte header followed by
 *   the data fork and the resource fork:
 *     0x00  "DDAR"
 *     0x08  u8   name length; the name is at 0x09, at most 63 bytes are kept
 *     0x48  u8   nonzero: folder start (names the folder)
 *     0x49  u8   nonzero: folder end (closes the innermost open folder)
 *     0x4a  u32  data fork size
 *     0x4e  u32  resource fork size
 *     0x76  u16  16-bit sum of the data fork's bytes
 *     0x78  u16  16-bit sum of the resource fork's bytes
 *     0x7a  u16  CRC over 0x00..0x79
 *   A file whose data fork is a complete single compressed file (section 1)
 *   and which has no resource fork of its own is decoded like one. Any other
 *   file is published with its forks as stored, proven by the two sums.
 *
 * Members. A file becomes one member per fork: the data fork under the
 * file's name and the resource fork under the same name with ".rsrc"
 * appended. A fork is listed only when it has content, except that a file
 * with no resource fork always lists its data fork. A standalone compressed
 * file has no name of its own (the Macintosh kept it in the file system), so
 * its members are "unpacked" and "unpacked.rsrc". Archive folders that hold
 * nothing are listed as folder records so they survive extraction.
 *
 * Archive names are Mac OS Roman and are converted to UTF-8. Characters a
 * file system would read as structure or refuse ('/', '\\', ':', '*', '?',
 * '"', '<', '>', '|', control codes) become '_', trailing dots and spaces
 * are dropped, Windows device names get a '_' prefix, and two members that
 * would land on the same output path (compared without case) are kept apart
 * with a numeric suffix. The name handling is adapted from this library's
 * own src/formats/compactpro/xx_compactpro.c (MIT).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/diskdoubler/xx_diskdoubler.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/diskdoubler/xx_diskdoubler.h"
#include "xxfclib/algo/compactpro/xx_compactpro.h"
#include <stdio.h>

#define XX_DISKDOUBLER_MAGIC 0xABCD0054U
#define XX_DISKDOUBLER_DDA2_MAGIC 0x44444132U /* "DDA2" */
#define XX_DISKDOUBLER_DDAR_MAGIC 0x44444152U /* "DDAR" */
#define XX_DISKDOUBLER_CRC_OFFSET 82
#define XX_DISKDOUBLER_OFF_DATA_DELTA 54
#define XX_DISKDOUBLER_OFF_RSRC_DELTA 56
/* DiskDoubler is a floppy-era format, and a half-gigabyte file of it is
 * noise. */
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
/* Method 8's payload is the Compact Pro LZH stream behind a 16-byte
 * DiskDoubler-specific preamble that is not part of the coded stream. */
#define XX_DISKDOUBLER_CPT_PREAMBLE 16
/* Both fork sizes are attacker-controlled 32-bit fields: refuse rather than
 * attempt the allocation. */
#define XX_DISKDOUBLER_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* DDA2 layout. */
#define XX_DISKDOUBLER_DDA2_HEADER 62
#define XX_DISKDOUBLER_DDA2_OFF_SIZE 36
#define XX_DISKDOUBLER_DDA2_OFF_END 40
#define XX_DISKDOUBLER_DDA2_END_TYPE 0xBBBBU
#define XX_DISKDOUBLER_DDA2_END_SIZE 6
#define XX_DISKDOUBLER_DDA2_FOOTER 16
#define XX_DISKDOUBLER_DDA2_FOOTER_MAGIC 0xBECDU
#define XX_DISKDOUBLER_DDA2_FOLDER 0x8000U
#define XX_DISKDOUBLER_DDA2_PACKED 0x4000U
#define XX_DISKDOUBLER_DDA2_FOLDER_HEADER 88
#define XX_DISKDOUBLER_DDA2_PACKED_HEADER 56
#define XX_DISKDOUBLER_DDA2_STORED_HEADER 90
#define XX_DISKDOUBLER_DDA2_NAME_MAX 31
#define XX_DISKDOUBLER_DDA2_OFF_PARENT 38
#define XX_DISKDOUBLER_DDA2_OFF_RECORD_SIZE 42
#define XX_DISKDOUBLER_DDA2_OFF_FOLDER_ID 62
#define XX_DISKDOUBLER_DDA2_OFF_DATA_SIZE 78
#define XX_DISKDOUBLER_DDA2_OFF_RSRC_SIZE 82
#define XX_DISKDOUBLER_DDA2_OFF_DATA_XOR 86
#define XX_DISKDOUBLER_DDA2_OFF_RSRC_XOR 87
/* Parent ids at or below this are "outside every folder". */
#define XX_DISKDOUBLER_DDA2_ROOT_PARENT 1U

/* DDAR layout. */
#define XX_DISKDOUBLER_DDAR_HEADER 78
#define XX_DISKDOUBLER_DDAR_RECORD 124
#define XX_DISKDOUBLER_DDAR_NAME_MAX 63
#define XX_DISKDOUBLER_DDAR_OFF_FOLDER 72
#define XX_DISKDOUBLER_DDAR_OFF_FOLDER_END 73
#define XX_DISKDOUBLER_DDAR_OFF_DATA_SIZE 74
#define XX_DISKDOUBLER_DDAR_OFF_RSRC_SIZE 78
#define XX_DISKDOUBLER_DDAR_OFF_DATA_SUM 118
#define XX_DISKDOUBLER_DDAR_OFF_RSRC_SUM 120

/* Hostile-input caps for the archive forms. A real archive fits on a few
 * floppies and holds hundreds of records. */
#define XX_DISKDOUBLER_MAX_RECORDS 65536U
#define XX_DISKDOUBLER_MAX_DEPTH 128U
#define XX_DISKDOUBLER_MAX_PATH 0x1000U
#define XX_DISKDOUBLER_MAX_NAME_BYTES ((size_t)0x2000000)
#define XX_DISKDOUBLER_MAX_CLAIM_ATTEMPTS 0x40000U
#define XX_DISKDOUBLER_MAX_PROBES 1024U
/* DiskDoubler numbers folders from 2 upwards, one per folder, so no real id
 * exceeds the record cap. Larger ids are not registered: their children are
 * placed at the top, like children of an unknown folder. */
#define XX_DISKDOUBLER_MAX_FOLDER_ID (XX_DISKDOUBLER_MAX_RECORDS + 2U)
/* A device-name prefix, 63 characters of at most three UTF-8 bytes, a NUL. */
#define XX_DISKDOUBLER_COMPONENT_BUFFER (1U + 63U * 3U + 1U)

/* How a member's bytes are produced. */
#define XX_DISKDOUBLER_KIND_DD 0U     /* a fork of a compressed file */
#define XX_DISKDOUBLER_KIND_RAW 1U    /* a fork stored as is */
#define XX_DISKDOUBLER_KIND_FOLDER 2U /* an empty archive folder */

/* How a stored fork proves itself. */
#define XX_DISKDOUBLER_CHECK_NONE 0U
#define XX_DISKDOUBLER_CHECK_XOR8 1U  /* DDA2: XOR of every byte */
#define XX_DISKDOUBLER_CHECK_SUM16 2U /* DDAR: 16-bit sum of every byte */

typedef struct xx_diskdoubler_member_s {
    char *name;
    /* For a compressed fork this is the 84-byte file header the decoder
     * re-reads; otherwise the archive record's header. */
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    uint16_t check_value;
    uint8_t check;
    uint8_t kind;
    bool is_folder;
} xx_diskdoubler_member;

typedef struct xx_diskdoubler_stream_s {
    xx_diskdoubler_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
} xx_diskdoubler_stream;

/* The two forks of one compressed file, as its 84-byte header states them. */
typedef struct xx_diskdoubler_forks_s {
    int64_t data_plain;
    int64_t data_packed;
    int64_t rsrc_plain;
    int64_t rsrc_packed;
    int64_t end; /* bytes from the header start to the end of both forks */
    uint8_t data_method;
    uint8_t rsrc_method;
    bool want_data;
    bool want_rsrc;
} xx_diskdoubler_forks;

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

static uint32_t xx_diskdoubler_be32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static uint16_t xx_diskdoubler_be16(const uint8_t *data) {
    return (uint16_t)(((uint32_t)data[0] << 8) | (uint32_t)data[1]);
}

/* CRC-16/CCITT, MSB-first, zero seed, no final xor. Every header of every
 * DiskDoubler form carries one, and it is what keeps a file whose first
 * bytes merely look like a signature from being claimed. */
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

/* A header whose last two bytes are the CRC of the bytes before them. */
static bool xx_diskdoubler_crc_ok(const uint8_t *header, int32_t size) {
    return size >= 2 && xx_diskdoubler_header_crc(header, size - 2) ==
                            xx_diskdoubler_be16(header + size - 2);
}

static char xx_diskdoubler_upper_ascii(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or
 * without an extension, in any case. */
static bool xx_diskdoubler_is_device(const char *name, size_t length) {
    static const char *const words[] = {"CON", "PRN", "AUX", "NUL",
                                        "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U;
    size_t word;
    size_t index;

    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (word = 0U; word < sizeof(words) / sizeof(words[0]); ++word) {
        const char *text = words[word];
        for (index = 0U; index < stem && text[index]; ++index) {
            if (xx_diskdoubler_upper_ascii(name[index]) != text[index]) break;
        }
        if (index == stem && text[index] == '\0') return true;
    }
    if (stem == 4U && name[3] >= '0' && name[3] <= '9') {
        char a = xx_diskdoubler_upper_ascii(name[0]);
        char b = xx_diskdoubler_upper_ascii(name[1]);
        char c = xx_diskdoubler_upper_ascii(name[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T')) {
            return true;
        }
    }
    return false;
}

/* Last line of defence at extraction time: nothing absolute, no drive
 * letter, no empty or dot component, no backslash, colon or control byte,
 * no device name. The listing never builds such a name; this only proves
 * it. */
static bool xx_diskdoubler_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') {
            if ((uint8_t)*end < 0x20U || *end == '\\' || *end == ':') {
                return false;
            }
            ++end;
        }
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 1U && cursor[0] == '.') return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        if (xx_diskdoubler_is_device(cursor, length)) return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_diskdoubler_stream_free(void *pointer) {
    xx_diskdoubler_stream *stream = (xx_diskdoubler_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    }
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of its name. On failure the name is
 * NOT freed; the caller still owns it. */
static bool xx_diskdoubler_add(xx_diskdoubler_stream *stream,
                               const xx_diskdoubler_member *member) {
    if (stream->count >= stream->capacity) {
        size_t limit = (size_t)XX_DISKDOUBLER_MAX_RECORDS * 2U + 2U;
        size_t grown = stream->capacity ? stream->capacity * 2U : 4U;
        xx_diskdoubler_member *items;
        if (grown > limit) grown = limit;
        if (stream->count >= grown) return false;
        items = (xx_diskdoubler_member *)xx_mem_realloc(
            stream->items, sizeof(*items) * grown);
        if (!items) return false;
        stream->items = items;
        stream->capacity = grown;
    }
    stream->items[stream->count++] = *member;
    return true;
}

static char *xx_diskdoubler_copy_string(const char *text) {
    size_t length = xx_str_len(text);
    char *copy = (char *)xx_mem_alloc(length + 1U);

    if (copy) xx_rt_memcpy(copy, text, length + 1U);
    return copy;
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

/* Validate one compressed file's 84-byte header, with @p room bytes
 * available from the header's first byte. Everything the header states must
 * fit in that room. */
static bool xx_diskdoubler_check_forks(const uint8_t *header, int64_t room,
                                       xx_diskdoubler_forks *forks) {
    uint16_t stored_crc;

    xx_mem_zero(forks, sizeof(*forks));
    if (room < XX_DISKDOUBLER_HEADER_SIZE) return false;
    if (xx_diskdoubler_be32(header) != XX_DISKDOUBLER_MAGIC) return false;

    stored_crc = xx_diskdoubler_be16(header + XX_DISKDOUBLER_CRC_OFFSET);
    /* Very early DiskDoubler releases left the CRC field zero. A zero word is
     * therefore "no CRC recorded" rather than "CRC of zero", and skipping the
     * check is the documented behaviour, not a loosening. Every other value
     * must match exactly. */
    if (stored_crc != 0U &&
        xx_diskdoubler_header_crc(header, XX_DISKDOUBLER_CRC_OFFSET) !=
            stored_crc) {
        return false;
    }

    forks->data_plain =
        (int64_t)xx_diskdoubler_be32(header + XX_DISKDOUBLER_OFF_DATA_PLAIN);
    forks->data_packed =
        (int64_t)xx_diskdoubler_be32(header + XX_DISKDOUBLER_OFF_DATA_PACKED);
    forks->rsrc_plain =
        (int64_t)xx_diskdoubler_be32(header + XX_DISKDOUBLER_OFF_RSRC_PLAIN);
    forks->rsrc_packed =
        (int64_t)xx_diskdoubler_be32(header + XX_DISKDOUBLER_OFF_RSRC_PACKED);
    forks->data_method = header[XX_DISKDOUBLER_OFF_DATA_METHOD];
    forks->rsrc_method = header[XX_DISKDOUBLER_OFF_RSRC_METHOD];

    /* A nonzero delta selector means a pre-filter was applied to the fork
     * before compression. No decoder here undoes one, and decoding without it
     * produces plausible-looking garbage, so the whole file is refused. */
    if (xx_diskdoubler_be16(header + XX_DISKDOUBLER_OFF_DATA_DELTA) != 0U ||
        xx_diskdoubler_be16(header + XX_DISKDOUBLER_OFF_RSRC_DELTA) != 0U) {
        return false;
    }

    /* A file with an empty data fork and a nonempty resource fork is a normal
     * Macintosh file (an application, a font suitcase), and the header then
     * says nothing meaningful in the data fork's method byte. In every other
     * case the data fork is published, so its method must be one this format
     * defines. */
    forks->want_data = (forks->data_plain != 0) || (forks->rsrc_plain == 0);
    forks->want_rsrc = forks->rsrc_plain != 0;
    if (forks->want_data && forks->data_plain != 0 &&
        !xx_diskdoubler_method_known(forks->data_method)) {
        return false;
    }
    if (forks->want_rsrc && !xx_diskdoubler_method_known(forks->rsrc_method)) {
        return false;
    }

    /* Both packed sizes are below 2^32, so the sum cannot overflow. */
    forks->end = XX_DISKDOUBLER_HEADER_SIZE + forks->data_packed +
                 forks->rsrc_packed;
    if (forks->end > room) return false;

    /* A stored fork's two lengths are the same number written twice. A
     * mismatch means the method byte is not describing this stream, which is
     * the cheapest way a random file with a valid-looking header gives itself
     * away. */
    if ((forks->data_method & 0x7fU) == 0U &&
        forks->data_packed != forks->data_plain) {
        return false;
    }
    if ((forks->rsrc_method & 0x7fU) == 0U &&
        forks->rsrc_packed != forks->rsrc_plain) {
        return false;
    }
    return true;
}

static void xx_diskdoubler_member_zero(xx_diskdoubler_member *member) {
    xx_mem_zero(member, sizeof(*member));
    /* The Macintosh dates (seconds since 1904-01-01 local time, with no zone
     * recorded) are left unset: converting them to the UNIX epoch would
     * invent a zone the file does not state. */
    member->timestamp = 0U;
}

/* Publish the listed forks of the compressed file whose header is at
 * @p header_offset (absolute). Takes ownership of @p data_name and
 * @p rsrc_name only on success; a NULL name means that fork is not listed. */
static bool xx_diskdoubler_publish_forks(xx_diskdoubler_stream *stream,
                                         int64_t header_offset,
                                         const xx_diskdoubler_forks *forks,
                                         char *data_name, char *rsrc_name) {
    xx_diskdoubler_member member;
    size_t first = stream->count;

    if (data_name) {
        xx_diskdoubler_member_zero(&member);
        member.name = data_name;
        member.kind = XX_DISKDOUBLER_KIND_DD;
        member.header_offset = header_offset;
        member.header_size = XX_DISKDOUBLER_HEADER_SIZE;
        member.data_offset = header_offset + XX_DISKDOUBLER_HEADER_SIZE;
        member.compressed_size = forks->data_packed;
        member.uncompressed_size = forks->data_plain;
        /* The container's own byte, high bit and all; decode masks it. When
         * the data fork is empty it is a stored zero-length member whatever
         * its method byte says: there is no stream for a codec. */
        member.method =
            forks->data_plain != 0 ? (uint32_t)forks->data_method : 0U;
        if (!xx_diskdoubler_add(stream, &member)) return false;
    }
    if (rsrc_name) {
        xx_diskdoubler_member_zero(&member);
        member.name = rsrc_name;
        member.kind = XX_DISKDOUBLER_KIND_DD;
        member.header_offset = header_offset;
        member.header_size = XX_DISKDOUBLER_HEADER_SIZE;
        member.data_offset = header_offset + XX_DISKDOUBLER_HEADER_SIZE +
                             forks->data_packed;
        member.compressed_size = forks->rsrc_packed;
        member.uncompressed_size = forks->rsrc_plain;
        member.method = (uint32_t)forks->rsrc_method;
        if (!xx_diskdoubler_add(stream, &member)) {
            /* Hand the data fork's name back too: the caller frees both. */
            if (stream->count > first) stream->count = first;
            return false;
        }
    }
    return true;
}

/* Publish a fork that is stored as is inside an archive record. Takes
 * ownership of @p name only on success. */
static bool xx_diskdoubler_publish_raw(xx_diskdoubler_stream *stream,
                                       char *name, int64_t header_offset,
                                       int64_t header_size, int64_t offset,
                                       int64_t size, uint8_t check,
                                       uint16_t check_value) {
    xx_diskdoubler_member member;

    xx_diskdoubler_member_zero(&member);
    member.name = name;
    member.kind = XX_DISKDOUBLER_KIND_RAW;
    member.header_offset = header_offset;
    member.header_size = header_size;
    member.data_offset = offset;
    member.compressed_size = size;
    member.uncompressed_size = size;
    member.method = XX_DISKDOUBLER_METHOD_STORE;
    member.check = check;
    member.check_value = check_value;
    return xx_diskdoubler_add(stream, &member);
}

/* --------------------------------------------------------------- names -- */

/* Mac OS Roman 0x80..0xFF as Unicode (Apple's ROMAN.TXT, with the euro sign
 * Mac OS 8.5 put at 0xDB). */
static const uint16_t xx_diskdoubler_mac_roman[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

static size_t xx_diskdoubler_put_utf8(char *out, uint32_t code) {
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12));
    out[1] = (char)(0x80U | ((code >> 6) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

/* One stored Mac OS Roman name (at most 63 bytes) made into one safe UTF-8
 * path component. Returns its length; @p out must hold
 * XX_DISKDOUBLER_COMPONENT_BUFFER bytes. */
static size_t xx_diskdoubler_component(const uint8_t *bytes, size_t size,
                                       char *out) {
    size_t input;
    size_t output = 0U;

    if (size > 63U) size = 63U;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|') {
            out[output++] = '_';
        } else if (c < 0x80U) {
            out[output++] = (char)c;
        } else {
            output += xx_diskdoubler_put_utf8(
                out + output, xx_diskdoubler_mac_roman[c - 0x80U]);
        }
    }
    /* Windows drops these, which would let "." and ".." through. */
    while (output != 0U &&
           (out[output - 1U] == ' ' || out[output - 1U] == '.')) {
        --output;
    }
    if (output == 0U) out[output++] = '_';
    if (xx_diskdoubler_is_device(out, output)) {
        xx_rt_memmove(out + 1, out, output);
        out[0] = '_';
        ++output;
    }
    out[output] = '\0';
    return output;
}

/* Output paths already handed out, compared the way a case-insensitive file
 * system would (ASCII plus the Latin letters Mac OS Roman carries in both
 * cases). Each slot also remembers the next numeric suffix to try for that
 * path, so a run of duplicates stays linear. The table grows so that it is
 * never more than half full, and a probe that runs longer than
 * XX_DISKDOUBLER_MAX_PROBES slots (only crafted collisions do that) fails
 * the parse rather than letting it go quadratic. */
typedef struct xx_diskdoubler_names_s {
    const char **slots;
    uint32_t *hints;
    size_t mask;
    size_t used;
} xx_diskdoubler_names;

static uint32_t xx_diskdoubler_fold_next(const char **cursor) {
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t code = s[0];
    size_t used = 1U;

    if (code == 0U) return 0U;
    if ((code & 0xE0U) == 0xC0U && s[1] != 0U) {
        code = ((code & 0x1FU) << 6) | (s[1] & 0x3FU);
        used = 2U;
    } else if ((code & 0xF0U) == 0xE0U && s[1] != 0U && s[2] != 0U) {
        code = ((code & 0x0FU) << 12) | ((uint32_t)(s[1] & 0x3FU) << 6) |
               (s[2] & 0x3FU);
        used = 3U;
    }
    *cursor += used;
    /* Folding more than the file system does only costs a suffix; folding
     * less could let two members share one file. */
    if (code >= 'a' && code <= 'z') return code - 0x20U;
    if (code >= 0xE0U && code <= 0xFEU && code != 0xF7U) return code - 0x20U;
    if (code == 0xFFU) return 0x178U;
    if (code == 0x153U) return 0x152U;
    if (code == 0x131U) return 'I';
    if (code == 0x3C0U) return 0x3A0U;
    return code;
}

static uint32_t xx_diskdoubler_hash(const char *name) {
    uint32_t hash = 2166136261U;
    uint32_t code;

    while ((code = xx_diskdoubler_fold_next(&name)) != 0U) {
        hash ^= code;
        hash *= 16777619U;
    }
    return hash;
}

static bool xx_diskdoubler_same(const char *left, const char *right) {
    for (;;) {
        uint32_t a = xx_diskdoubler_fold_next(&left);
        uint32_t b = xx_diskdoubler_fold_next(&right);
        if (a != b) return false;
        if (a == 0U) return true;
    }
}

/* The slot holding @p name, or the empty slot where it belongs. False when
 * the probe ran too long. */
static bool xx_diskdoubler_names_find(const xx_diskdoubler_names *names,
                                      const char *name, size_t *found) {
    size_t slot = (size_t)xx_diskdoubler_hash(name) & names->mask;
    uint32_t probes = 0U;

    while (names->slots[slot] &&
           !xx_diskdoubler_same(names->slots[slot], name)) {
        if (++probes > XX_DISKDOUBLER_MAX_PROBES) return false;
        slot = (slot + 1U) & names->mask;
    }
    *found = slot;
    return true;
}

static void xx_diskdoubler_names_cleanup(xx_diskdoubler_names *names) {
    if (names->slots) xx_mem_free((void *)names->slots);
    if (names->hints) xx_mem_free(names->hints);
    xx_mem_zero(names, sizeof(*names));
}

/* Make room for two more names, keeping the table at most half full. */
static bool xx_diskdoubler_names_reserve(xx_diskdoubler_names *names) {
    size_t size = names->slots ? names->mask + 1U : 0U;
    size_t grown;
    const char **slots;
    uint32_t *hints;
    size_t index;

    if (size != 0U && (names->used + 2U) * 2U <= size) return true;
    grown = size ? size * 2U : 64U;
    slots = (const char **)xx_mem_calloc(grown, sizeof(*slots));
    hints = (uint32_t *)xx_mem_calloc(grown, sizeof(*hints));
    if (!slots || !hints) {
        if (slots) xx_mem_free((void *)slots);
        if (hints) xx_mem_free(hints);
        return false;
    }
    /* The new table is at most a quarter full, so every probe below ends. */
    for (index = 0U; index < size; ++index) {
        if (names->slots[index]) {
            size_t slot =
                (size_t)xx_diskdoubler_hash(names->slots[index]) & (grown - 1U);
            while (slots[slot]) slot = (slot + 1U) & (grown - 1U);
            slots[slot] = names->slots[index];
            hints[slot] = names->hints[index];
        }
    }
    if (names->slots) xx_mem_free((void *)names->slots);
    if (names->hints) xx_mem_free(names->hints);
    names->slots = slots;
    names->hints = hints;
    names->mask = grown - 1U;
    return true;
}

/* ------------------------------------------------------ archive build -- */

typedef struct xx_diskdoubler_folder_s {
    char *path;            /* owned here; the name table points at it */
    int64_t header_offset;
    int64_t header_size;
    bool has_child;
} xx_diskdoubler_folder;

/* DDA2 folder ids, indexed directly: 0 = unknown, 1 = the top of the output,
 * n + 2 = folder n. */
typedef struct xx_diskdoubler_ids_s {
    uint32_t *values;
    size_t size;
} xx_diskdoubler_ids;

typedef struct xx_diskdoubler_build_s {
    Abstractformat *self;
    xx_pd_struct *pd;
    xx_diskdoubler_stream *stream;
    xx_diskdoubler_names names;
    xx_diskdoubler_ids ids;
    xx_diskdoubler_folder *folders;
    size_t folder_count;
    size_t folder_capacity;
    char **owned;          /* reserved stems no member owns */
    size_t owned_count;
    size_t owned_capacity;
    size_t stack[XX_DISKDOUBLER_MAX_DEPTH]; /* DDAR's open folders */
    size_t depth;
    size_t name_bytes;
    uint32_t records;
} xx_diskdoubler_build;

static void xx_diskdoubler_build_cleanup(xx_diskdoubler_build *build) {
    size_t index;

    xx_diskdoubler_names_cleanup(&build->names);
    if (build->ids.values) xx_mem_free(build->ids.values);
    build->ids.values = NULL;
    build->ids.size = 0U;
    for (index = 0U; index < build->folder_count; ++index) {
        xx_mem_free(build->folders[index].path);
    }
    if (build->folders) xx_mem_free(build->folders);
    for (index = 0U; index < build->owned_count; ++index) {
        xx_mem_free(build->owned[index]);
    }
    if (build->owned) xx_mem_free(build->owned);
    build->folders = NULL;
    build->owned = NULL;
    build->folder_count = 0U;
    build->owned_count = 0U;
}

static bool xx_diskdoubler_keep(xx_diskdoubler_build *build, char *name) {
    if (build->owned_count >= build->owned_capacity) {
        size_t grown = build->owned_capacity ? build->owned_capacity * 2U : 16U;
        char **owned = (char **)xx_mem_realloc(build->owned,
                                               sizeof(*owned) * grown);
        if (!owned) return false;
        build->owned = owned;
        build->owned_capacity = grown;
    }
    build->owned[build->owned_count++] = name;
    return true;
}

static char *xx_diskdoubler_budget_alloc(xx_diskdoubler_build *build,
                                         size_t size) {
    if (size > XX_DISKDOUBLER_MAX_NAME_BYTES - build->name_bytes) return NULL;
    build->name_bytes += size;
    return (char *)xx_mem_alloc(size);
}

static void xx_diskdoubler_release(xx_diskdoubler_build *build, char *name) {
    if (!name) return;
    build->name_bytes -= xx_str_len(name) + 1U;
    xx_mem_free(name);
}

/* "<parent>/<stem>[_<n>]<extension>[.rsrc]", or NULL when it would be too
 * long or memory runs out. */
static char *xx_diskdoubler_candidate(xx_diskdoubler_build *build,
                                      const char *parent, const char *component,
                                      size_t component_length, uint32_t suffix,
                                      bool resource) {
    char digits[16];
    size_t digit_count = 0U;
    size_t parent_length = parent ? xx_str_len(parent) : 0U;
    size_t stem = component_length;
    size_t total;
    size_t index;
    char *result;
    char *cursor;

    if (suffix != 0U) {
        int written = xx_rt_snprintf(digits, sizeof(digits), "_%u",
                                     (unsigned)suffix);
        if (written <= 0 || (size_t)written >= sizeof(digits)) return NULL;
        digit_count = (size_t)written;
        /* The number goes before a final extension, never at the start. */
        for (index = component_length; index > 1U; --index) {
            if (component[index - 1U] == '.') {
                stem = index - 1U;
                break;
            }
        }
    }
    total = parent_length + (parent_length ? 1U : 0U) + component_length +
            digit_count + (resource ? 5U : 0U);
    if (total > (size_t)XX_DISKDOUBLER_MAX_PATH) return NULL;
    result = xx_diskdoubler_budget_alloc(build, total + 1U);
    if (!result) return NULL;
    cursor = result;
    if (parent_length) {
        xx_rt_memcpy(cursor, parent, parent_length);
        cursor += parent_length;
        *cursor++ = '/';
    }
    xx_rt_memcpy(cursor, component, stem);
    cursor += stem;
    if (digit_count) {
        xx_rt_memcpy(cursor, digits, digit_count);
        cursor += digit_count;
    }
    xx_rt_memcpy(cursor, component + stem, component_length - stem);
    cursor += component_length - stem;
    if (resource) {
        xx_rt_memcpy(cursor, ".rsrc", 5U);
        cursor += 5;
    }
    *cursor = '\0';
    return result;
}

/* Pick the first free "<stem>[_<n>]" under @p parent. The plain path is
 * always reserved (a folder, a data fork, or just the stem a resource fork
 * hangs off), the ".rsrc" path too when @p want_rsrc. The caller must give
 * both returned strings an owner (a member, a folder, or the keep list). */
static bool xx_diskdoubler_claim(xx_diskdoubler_build *build,
                                 const char *parent, const char *component,
                                 size_t length, bool want_rsrc, char **plain,
                                 char **rsrc) {
    size_t base_slot = (size_t)-1;
    uint32_t suffix = 0U;
    uint32_t attempts;

    *plain = NULL;
    *rsrc = NULL;
    if (!xx_diskdoubler_names_reserve(&build->names)) return false;
    for (attempts = 0U; attempts < XX_DISKDOUBLER_MAX_CLAIM_ATTEMPTS;
         ++attempts) {
        char *candidate = xx_diskdoubler_candidate(build, parent, component,
                                                   length, suffix, false);
        char *resource = NULL;
        size_t slot = 0U;
        size_t rsrc_slot = 0U;
        bool free_name;

        if (!candidate) return false;
        if (!xx_diskdoubler_names_find(&build->names, candidate, &slot)) {
            xx_diskdoubler_release(build, candidate);
            return false;
        }
        free_name = build->names.slots[slot] == NULL;
        if (free_name && want_rsrc) {
            resource = xx_diskdoubler_candidate(build, parent, component,
                                                length, suffix, true);
            if (!resource ||
                !xx_diskdoubler_names_find(&build->names, resource,
                                           &rsrc_slot)) {
                xx_diskdoubler_release(build, resource);
                xx_diskdoubler_release(build, candidate);
                return false;
            }
            free_name = build->names.slots[rsrc_slot] == NULL;
        }
        if (free_name) {
            build->names.slots[slot] = candidate;
            build->names.hints[slot] = 1U;
            ++build->names.used;
            if (resource) {
                /* Re-found: the plain name may have taken its slot. */
                if (!xx_diskdoubler_names_find(&build->names, resource,
                                               &rsrc_slot)) {
                    /* The plain name is in the table and owned by nobody
                     * yet; the build is abandoned, so drop the pointer. */
                    build->names.slots[slot] = NULL;
                    --build->names.used;
                    xx_diskdoubler_release(build, resource);
                    xx_diskdoubler_release(build, candidate);
                    return false;
                }
                build->names.slots[rsrc_slot] = resource;
                build->names.hints[rsrc_slot] = 1U;
                ++build->names.used;
            }
            if (base_slot != (size_t)-1) {
                build->names.hints[base_slot] = suffix + 1U;
            }
            *plain = candidate;
            *rsrc = resource;
            return true;
        }
        if (suffix == 0U) {
            /* Continue from where the last clash on this name stopped. The
             * plain name was just found, so this lookup cannot run long. */
            if (!xx_diskdoubler_names_find(&build->names, candidate,
                                           &base_slot)) {
                xx_diskdoubler_release(build, resource);
                xx_diskdoubler_release(build, candidate);
                return false;
            }
            if (build->names.slots[base_slot]) {
                suffix = build->names.hints[base_slot];
                if (suffix == 0U) suffix = 1U;
            } else {
                suffix = 1U;
                base_slot = (size_t)-1;
            }
        } else {
            ++suffix;
        }
        xx_diskdoubler_release(build, resource);
        xx_diskdoubler_release(build, candidate);
    }
    return false;
}

/* Make @p parent (a folder path, or NULL for the top) the home of a new
 * folder named by @p name, and return that folder's index. */
static bool xx_diskdoubler_new_folder(xx_diskdoubler_build *build,
                                      const char *parent, const uint8_t *name,
                                      size_t name_size, int64_t header_offset,
                                      int64_t header_size, size_t *index) {
    char component[XX_DISKDOUBLER_COMPONENT_BUFFER];
    size_t length = xx_diskdoubler_component(name, name_size, component);
    char *plain = NULL;
    char *rsrc = NULL;
    xx_diskdoubler_folder *folder;

    if (build->folder_count >= build->folder_capacity) {
        size_t grown =
            build->folder_capacity ? build->folder_capacity * 2U : 16U;
        xx_diskdoubler_folder *folders = (xx_diskdoubler_folder *)
            xx_mem_realloc(build->folders, sizeof(*folders) * grown);
        if (!folders) return false;
        build->folders = folders;
        build->folder_capacity = grown;
    }
    if (!xx_diskdoubler_claim(build, parent, component, length, false, &plain,
                              &rsrc)) {
        return false;
    }
    folder = &build->folders[build->folder_count];
    folder->path = plain;
    folder->header_offset = header_offset;
    folder->header_size = header_size;
    folder->has_child = false;
    *index = build->folder_count++;
    return true;
}

/* Publish a compressed file found inside an archive record. */
static bool xx_diskdoubler_add_packed(xx_diskdoubler_build *build,
                                      const char *parent, const uint8_t *name,
                                      size_t name_size, int64_t header_offset,
                                      const xx_diskdoubler_forks *forks) {
    char component[XX_DISKDOUBLER_COMPONENT_BUFFER];
    size_t length = xx_diskdoubler_component(name, name_size, component);
    char *plain = NULL;
    char *rsrc = NULL;

    if (!xx_diskdoubler_claim(build, parent, component, length,
                              forks->want_rsrc, &plain, &rsrc)) {
        return false;
    }
    if (!xx_diskdoubler_publish_forks(build->stream, header_offset, forks,
                                      forks->want_data ? plain : NULL, rsrc)) {
        /* Neither name found an owner; the name table still points at them
         * but is discarded with the build. */
        xx_mem_free(plain);
        if (rsrc) xx_mem_free(rsrc);
        return false;
    }
    /* A resource fork with no listed data fork: the stem was only reserved
     * and names no member, but the name table still points at it. */
    if (!forks->want_data && !xx_diskdoubler_keep(build, plain)) {
        xx_mem_free(plain);
        return false;
    }
    return true;
}

/* Publish a file whose forks are stored raw inside an archive record. */
static bool xx_diskdoubler_add_raw(xx_diskdoubler_build *build,
                                   const char *parent, const uint8_t *name,
                                   size_t name_size, int64_t header_offset,
                                   int64_t header_size, int64_t data_offset,
                                   int64_t data_size, int64_t rsrc_size,
                                   uint8_t check, uint16_t data_check,
                                   uint16_t rsrc_check) {
    char component[XX_DISKDOUBLER_COMPONENT_BUFFER];
    size_t length = xx_diskdoubler_component(name, name_size, component);
    bool want_rsrc = rsrc_size != 0;
    bool want_data = data_size != 0 || !want_rsrc;
    char *plain = NULL;
    char *rsrc = NULL;

    if (!xx_diskdoubler_claim(build, parent, component, length, want_rsrc,
                              &plain, &rsrc)) {
        return false;
    }
    if (want_data) {
        if (!xx_diskdoubler_publish_raw(build->stream, plain, header_offset,
                                        header_size, data_offset, data_size,
                                        check, data_check)) {
            xx_mem_free(plain);
            if (rsrc) xx_mem_free(rsrc);
            return false;
        }
    } else if (!xx_diskdoubler_keep(build, plain)) {
        xx_mem_free(plain);
        if (rsrc) xx_mem_free(rsrc);
        return false;
    }
    if (want_rsrc &&
        !xx_diskdoubler_publish_raw(build->stream, rsrc, header_offset,
                                    header_size, data_offset + data_size,
                                    rsrc_size, check, rsrc_check)) {
        xx_mem_free(rsrc);
        return false;
    }
    return true;
}

/* Folders nothing was placed in become folder records, so extraction
 * recreates them. */
static bool xx_diskdoubler_add_empty_folders(xx_diskdoubler_build *build) {
    size_t index;

    for (index = 0U; index < build->folder_count; ++index) {
        const xx_diskdoubler_folder *folder = &build->folders[index];
        xx_diskdoubler_member member;
        char *copy;

        if (folder->has_child) continue;
        copy = xx_diskdoubler_copy_string(folder->path);
        if (!copy) return false;
        xx_diskdoubler_member_zero(&member);
        member.name = copy;
        member.kind = XX_DISKDOUBLER_KIND_FOLDER;
        member.header_offset = folder->header_offset;
        member.header_size = folder->header_size;
        member.data_offset = folder->header_offset;
        member.is_folder = true;
        if (!xx_diskdoubler_add(build->stream, &member)) {
            xx_mem_free(copy);
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------- parsing -- */

/* A standalone compressed file. */
static bool xx_diskdoubler_parse_single(Abstractformat *self, int64_t span,
                                        xx_diskdoubler_stream *stream) {
    uint8_t header[XX_DISKDOUBLER_HEADER_SIZE];
    uint8_t trailer[XX_DISKDOUBLER_HEADER_SIZE];
    xx_diskdoubler_forks forks;
    int64_t trailing;
    char *data_name = NULL;
    char *rsrc_name = NULL;

    if (span < XX_DISKDOUBLER_HEADER_SIZE ||
        !xx_diskdoubler_read_at(self, self->base_address, header,
                                sizeof(header)) ||
        !xx_diskdoubler_check_forks(header, span, &forks)) {
        return false;
    }

    /* Nothing may follow the two forks except one exact copy of the header,
     * which some writers append. Accepting arbitrary trailing bytes would
     * turn this format into a prefix matcher for any file starting with the
     * magic. */
    trailing = span - forks.end;
    if (trailing != 0) {
        if (trailing != XX_DISKDOUBLER_HEADER_SIZE) return false;
        if (!xx_diskdoubler_read_at(self, self->base_address + forks.end,
                                    trailer, sizeof(trailer))) {
            return false;
        }
        if (xx_rt_memcmp(trailer, header, sizeof(header)) != 0) return false;
    }

    /* The two forks become two members, not one: they are separate streams
     * in every way the container cares about, and concatenating them would
     * need a wrapper (MacBinary, AppleDouble) the file does not contain. */
    if (forks.want_data) {
        data_name = xx_diskdoubler_copy_string(XX_DISKDOUBLER_DATA_NAME);
        if (!data_name) return false;
    }
    if (forks.want_rsrc) {
        rsrc_name = xx_diskdoubler_copy_string(XX_DISKDOUBLER_RSRC_NAME);
        if (!rsrc_name) {
            if (data_name) xx_mem_free(data_name);
            return false;
        }
    }
    if (!xx_diskdoubler_publish_forks(stream, self->base_address, &forks,
                                      data_name, rsrc_name)) {
        if (data_name) xx_mem_free(data_name);
        if (rsrc_name) xx_mem_free(rsrc_name);
        return false;
    }
    stream->archive_size = span;
    return stream->count != 0U;
}

/* A compressed file as a record's body at @p offset (relative to the base),
 * with @p room bytes of the record left for it. */
static bool xx_diskdoubler_read_forks(Abstractformat *self, int64_t offset,
                                      int64_t room,
                                      xx_diskdoubler_forks *forks) {
    uint8_t header[XX_DISKDOUBLER_HEADER_SIZE];

    return room >= XX_DISKDOUBLER_HEADER_SIZE &&
           xx_diskdoubler_read_at(self, self->base_address + offset, header,
                                  sizeof(header)) &&
           xx_diskdoubler_check_forks(header, room, forks);
}

/* Record @p value (1 = top, n + 2 = folder n) as what DDA2 folder id @p id
 * stands for. Ids beyond the cap are not recorded. */
static bool xx_diskdoubler_ids_set(xx_diskdoubler_ids *ids, uint32_t id,
                                   uint32_t value) {
    if (id > XX_DISKDOUBLER_MAX_FOLDER_ID) return true;
    if ((size_t)id >= ids->size) {
        size_t grown = ids->size ? ids->size : 64U;
        uint32_t *values;
        while (grown <= (size_t)id) grown *= 2U;
        values = (uint32_t *)xx_mem_realloc(ids->values,
                                            sizeof(*values) * grown);
        if (!values) return false;
        xx_mem_zero(values + ids->size,
                    sizeof(*values) * (grown - ids->size));
        ids->values = values;
        ids->size = grown;
    }
    ids->values[id] = value;
    return true;
}

/* The path a DDA2 record naming parent id @p id lives under, or NULL for
 * the top of the output. */
static const char *xx_diskdoubler_dda2_parent(xx_diskdoubler_build *build,
                                              uint32_t id) {
    uint32_t value;
    xx_diskdoubler_folder *folder;

    if (id <= XX_DISKDOUBLER_DDA2_ROOT_PARENT || (size_t)id >= build->ids.size) {
        return NULL;
    }
    value = build->ids.values[id];
    if (value < 2U || (size_t)(value - 2U) >= build->folder_count) return NULL;
    folder = &build->folders[value - 2U];
    folder->has_child = true;
    return folder->path;
}

/* The archive's end: the footer's stated extent when it checks out, else
 * the header's stated size when it agrees with the walk, else the end
 * record itself. */
static int64_t xx_diskdoubler_dda2_end(Abstractformat *self, int64_t span,
                                       const uint8_t *archive,
                                       int64_t end_record) {
    uint8_t footer[XX_DISKDOUBLER_DDA2_FOOTER];
    int64_t position = end_record + XX_DISKDOUBLER_DDA2_END_SIZE;
    int64_t stated_size =
        (int64_t)xx_diskdoubler_be32(archive + XX_DISKDOUBLER_DDA2_OFF_SIZE);

    if (xx_diskdoubler_range_within(span, position,
                                    XX_DISKDOUBLER_DDA2_FOOTER) &&
        xx_diskdoubler_read_at(self, self->base_address + position, footer,
                               sizeof(footer)) &&
        xx_diskdoubler_be16(footer) == XX_DISKDOUBLER_DDA2_FOOTER_MAGIC) {
        int64_t index_size = (int64_t)xx_diskdoubler_be32(footer + 2);
        int64_t index_start = position + XX_DISKDOUBLER_DDA2_FOOTER;
        /* The index is a few bytes per record; one this large is not. */
        if (index_size <= (int64_t)XX_DISKDOUBLER_MAX_RECORDS * 8 &&
            xx_diskdoubler_range_within(span, index_start, index_size)) {
            uint8_t *index = (uint8_t *)xx_mem_alloc(
                index_size ? (size_t)index_size : 1U);
            bool ok = index != NULL &&
                      xx_diskdoubler_read_at(self,
                                             self->base_address + index_start,
                                             index, (size_t)index_size) &&
                      xx_diskdoubler_header_crc(index, (int32_t)index_size) ==
                          xx_diskdoubler_be16(footer + 6);
            if (index) xx_mem_free(index);
            if (ok) return index_start + index_size;
        }
    }
    if ((int64_t)xx_diskdoubler_be32(archive + XX_DISKDOUBLER_DDA2_OFF_END) ==
            end_record &&
        stated_size >= position && stated_size <= span) {
        return stated_size;
    }
    return position;
}

static bool xx_diskdoubler_parse_dda2(xx_diskdoubler_build *build,
                                      int64_t span) {
    Abstractformat *self = build->self;
    uint8_t archive[XX_DISKDOUBLER_DDA2_HEADER];
    uint8_t record[XX_DISKDOUBLER_DDA2_STORED_HEADER];
    int64_t position = XX_DISKDOUBLER_DDA2_HEADER;

    if (span < XX_DISKDOUBLER_DDA2_HEADER + XX_DISKDOUBLER_DDA2_END_SIZE ||
        !xx_diskdoubler_read_at(self, self->base_address, archive,
                                sizeof(archive)) ||
        xx_diskdoubler_be32(archive) != XX_DISKDOUBLER_DDA2_MAGIC ||
        xx_diskdoubler_be16(archive + 4) != XX_DISKDOUBLER_DDA2_HEADER ||
        !xx_diskdoubler_crc_ok(archive, XX_DISKDOUBLER_DDA2_HEADER)) {
        return false;
    }

    for (;;) {
        uint32_t type;
        uint32_t parent_id;
        int64_t record_size;
        int32_t header_size;
        size_t name_size;
        const char *parent;

        if (build->pd && xx_pd_is_stopped(build->pd)) return false;
        /* No end record before the data runs out: not a whole archive. */
        if (!xx_diskdoubler_range_within(span, position,
                                         XX_DISKDOUBLER_DDA2_END_SIZE) ||
            !xx_diskdoubler_read_at(self, self->base_address + position,
                                    record, XX_DISKDOUBLER_DDA2_END_SIZE) ||
            xx_diskdoubler_be32(record) != XX_DISKDOUBLER_DDA2_MAGIC) {
            return false;
        }
        type = xx_diskdoubler_be16(record + 4);
        if (type == XX_DISKDOUBLER_DDA2_END_TYPE) break;
        if (++build->records > XX_DISKDOUBLER_MAX_RECORDS) return false;

        if (type & XX_DISKDOUBLER_DDA2_FOLDER) {
            header_size = XX_DISKDOUBLER_DDA2_FOLDER_HEADER;
        } else if (type & XX_DISKDOUBLER_DDA2_PACKED) {
            header_size = XX_DISKDOUBLER_DDA2_PACKED_HEADER;
        } else {
            /* A stored file, unless its header says otherwise; then the
             * compressed form, as The Unarchiver reads every file record. */
            header_size = XX_DISKDOUBLER_DDA2_STORED_HEADER;
            if (!xx_diskdoubler_range_within(span, position, header_size) ||
                !xx_diskdoubler_read_at(self, self->base_address + position,
                                        record, (size_t)header_size) ||
                !xx_diskdoubler_crc_ok(record, header_size)) {
                header_size = XX_DISKDOUBLER_DDA2_PACKED_HEADER;
            }
        }
        if (!xx_diskdoubler_range_within(span, position, header_size) ||
            !xx_diskdoubler_read_at(self, self->base_address + position,
                                    record, (size_t)header_size) ||
            !xx_diskdoubler_crc_ok(record, header_size)) {
            return false;
        }
        record_size = (int64_t)xx_diskdoubler_be32(
            record + XX_DISKDOUBLER_DDA2_OFF_RECORD_SIZE);
        if (record_size < header_size ||
            !xx_diskdoubler_range_within(span, position, record_size)) {
            return false;
        }
        parent_id = xx_diskdoubler_be32(record + XX_DISKDOUBLER_DDA2_OFF_PARENT);
        name_size = record[6];
        if (name_size > XX_DISKDOUBLER_DDA2_NAME_MAX) {
            name_size = XX_DISKDOUBLER_DDA2_NAME_MAX;
        }

        if (header_size == XX_DISKDOUBLER_DDA2_FOLDER_HEADER) {
            uint32_t id = xx_diskdoubler_be32(
                record + XX_DISKDOUBLER_DDA2_OFF_FOLDER_ID);
            if (parent_id <= XX_DISKDOUBLER_DDA2_ROOT_PARENT) {
                /* The archive's own folder: its children are the top. */
                if (!xx_diskdoubler_ids_set(&build->ids, id, 1U)) return false;
            } else {
                size_t index = 0U;
                parent = xx_diskdoubler_dda2_parent(build, parent_id);
                if (!xx_diskdoubler_new_folder(
                        build, parent, record + 7, name_size,
                        self->base_address + position, header_size, &index) ||
                    !xx_diskdoubler_ids_set(&build->ids, id,
                                            (uint32_t)index + 2U)) {
                    return false;
                }
            }
        } else if (header_size == XX_DISKDOUBLER_DDA2_PACKED_HEADER) {
            xx_diskdoubler_forks forks;
            parent = xx_diskdoubler_dda2_parent(build, parent_id);
            if (!xx_diskdoubler_read_forks(self, position + header_size,
                                           record_size - header_size,
                                           &forks) ||
                !xx_diskdoubler_add_packed(
                    build, parent, record + 7, name_size,
                    self->base_address + position + header_size, &forks)) {
                return false;
            }
        } else {
            int64_t data_size = (int64_t)xx_diskdoubler_be32(
                record + XX_DISKDOUBLER_DDA2_OFF_DATA_SIZE);
            int64_t rsrc_size = (int64_t)xx_diskdoubler_be32(
                record + XX_DISKDOUBLER_DDA2_OFF_RSRC_SIZE);
            parent = xx_diskdoubler_dda2_parent(build, parent_id);
            /* Both sizes are below 2^32, so the sum cannot overflow. */
            if (data_size + rsrc_size > record_size - header_size ||
                !xx_diskdoubler_add_raw(
                    build, parent, record + 7, name_size,
                    self->base_address + position, header_size,
                    self->base_address + position + header_size, data_size,
                    rsrc_size, XX_DISKDOUBLER_CHECK_XOR8,
                    record[XX_DISKDOUBLER_DDA2_OFF_DATA_XOR],
                    record[XX_DISKDOUBLER_DDA2_OFF_RSRC_XOR])) {
                return false;
            }
        }
        position += record_size;
    }

    build->stream->archive_size =
        xx_diskdoubler_dda2_end(self, span, archive, position);
    return xx_diskdoubler_add_empty_folders(build);
}

/* The folder a DDAR record lives in, or NULL for the top. */
static const char *xx_diskdoubler_ddar_parent(xx_diskdoubler_build *build) {
    xx_diskdoubler_folder *folder;

    if (build->depth == 0U) return NULL;
    folder = &build->folders[build->stack[build->depth - 1U]];
    folder->has_child = true;
    return folder->path;
}

static bool xx_diskdoubler_parse_ddar(xx_diskdoubler_build *build,
                                      int64_t span) {
    Abstractformat *self = build->self;
    uint8_t archive[XX_DISKDOUBLER_DDAR_HEADER];
    uint8_t record[XX_DISKDOUBLER_DDAR_RECORD];
    int64_t position = XX_DISKDOUBLER_DDAR_HEADER;

    if (span < XX_DISKDOUBLER_DDAR_HEADER ||
        !xx_diskdoubler_read_at(self, self->base_address, archive,
                                sizeof(archive)) ||
        xx_diskdoubler_be32(archive) != XX_DISKDOUBLER_DDAR_MAGIC ||
        !xx_diskdoubler_crc_ok(archive, XX_DISKDOUBLER_DDAR_HEADER)) {
        return false;
    }

    /* DDAR has no end record: the records run to the end of the data, and a
     * short or unrecognised record there is a damaged archive. */
    while (position < span) {
        int64_t data_size;
        int64_t rsrc_size;
        int64_t body;
        size_t name_size;
        const char *parent;
        xx_diskdoubler_forks forks;

        if (build->pd && xx_pd_is_stopped(build->pd)) return false;
        if (++build->records > XX_DISKDOUBLER_MAX_RECORDS) return false;
        if (!xx_diskdoubler_range_within(span, position,
                                         XX_DISKDOUBLER_DDAR_RECORD) ||
            !xx_diskdoubler_read_at(self, self->base_address + position,
                                    record, sizeof(record)) ||
            xx_diskdoubler_be32(record) != XX_DISKDOUBLER_DDAR_MAGIC ||
            !xx_diskdoubler_crc_ok(record, XX_DISKDOUBLER_DDAR_RECORD)) {
            return false;
        }
        name_size = record[8];
        if (name_size > XX_DISKDOUBLER_DDAR_NAME_MAX) {
            name_size = XX_DISKDOUBLER_DDAR_NAME_MAX;
        }
        data_size = (int64_t)xx_diskdoubler_be32(
            record + XX_DISKDOUBLER_DDAR_OFF_DATA_SIZE);
        rsrc_size = (int64_t)xx_diskdoubler_be32(
            record + XX_DISKDOUBLER_DDAR_OFF_RSRC_SIZE);
        body = position + XX_DISKDOUBLER_DDAR_RECORD;

        if (record[XX_DISKDOUBLER_DDAR_OFF_FOLDER] != 0U) {
            size_t index = 0U;
            if (data_size != 0 || rsrc_size != 0) return false;
            if (build->depth >= XX_DISKDOUBLER_MAX_DEPTH) return false;
            parent = xx_diskdoubler_ddar_parent(build);
            if (!xx_diskdoubler_new_folder(build, parent, record + 9,
                                           name_size,
                                           self->base_address + position,
                                           XX_DISKDOUBLER_DDAR_RECORD,
                                           &index)) {
                return false;
            }
            build->stack[build->depth++] = index;
            position = body;
            continue;
        }
        if (record[XX_DISKDOUBLER_DDAR_OFF_FOLDER_END] != 0U) {
            if (data_size != 0 || rsrc_size != 0) return false;
            /* A stray folder end at the top closes nothing. */
            if (build->depth != 0U) --build->depth;
            position = body;
            continue;
        }
        /* Both sizes are below 2^32, so the sum cannot overflow. */
        if (!xx_diskdoubler_range_within(span, body, data_size + rsrc_size)) {
            return false;
        }
        parent = xx_diskdoubler_ddar_parent(build);
        /* The usual case: the data fork holds a whole compressed file.
         * Anything else is kept as stored forks. */
        if (rsrc_size == 0 &&
            xx_diskdoubler_read_forks(self, body, data_size, &forks)) {
            if (!xx_diskdoubler_add_packed(build, parent, record + 9,
                                           name_size,
                                           self->base_address + body,
                                           &forks)) {
                return false;
            }
        } else if (!xx_diskdoubler_add_raw(
                       build, parent, record + 9, name_size,
                       self->base_address + position,
                       XX_DISKDOUBLER_DDAR_RECORD, self->base_address + body,
                       data_size, rsrc_size, XX_DISKDOUBLER_CHECK_SUM16,
                       xx_diskdoubler_be16(record +
                                           XX_DISKDOUBLER_DDAR_OFF_DATA_SUM),
                       xx_diskdoubler_be16(record +
                                           XX_DISKDOUBLER_DDAR_OFF_RSRC_SUM))) {
            return false;
        }
        position = body + data_size + rsrc_size;
    }
    build->stream->archive_size = position;
    return xx_diskdoubler_add_empty_folders(build);
}

static xx_diskdoubler_stream *xx_diskdoubler_parse(Abstractformat *self,
                                                   xx_pd_struct *pd) {
    xx_diskdoubler_stream *stream;
    uint8_t magic[4];
    int64_t total;
    int64_t span;
    uint32_t signature;
    bool ok = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)sizeof(magic)) return NULL;
    if (span > XX_DISKDOUBLER_MAX_SIZE) return NULL;
    if (pd && xx_pd_is_stopped(pd)) return NULL;
    if (!xx_diskdoubler_read_at(self, self->base_address, magic,
                                sizeof(magic))) {
        return NULL;
    }
    signature = xx_diskdoubler_be32(magic);
    if (signature != XX_DISKDOUBLER_MAGIC &&
        signature != XX_DISKDOUBLER_DDA2_MAGIC &&
        signature != XX_DISKDOUBLER_DDAR_MAGIC) {
        return NULL;
    }

    stream = (xx_diskdoubler_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    if (signature == XX_DISKDOUBLER_MAGIC) {
        ok = xx_diskdoubler_parse_single(self, span, stream);
    } else {
        xx_diskdoubler_build build;
        xx_mem_zero(&build, sizeof(build));
        build.self = self;
        build.pd = pd;
        build.stream = stream;
        ok = signature == XX_DISKDOUBLER_DDA2_MAGIC
                 ? xx_diskdoubler_parse_dda2(&build, span)
                 : xx_diskdoubler_parse_ddar(&build, span);
        xx_diskdoubler_build_cleanup(&build);
    }
    if (ok && pd && xx_pd_is_stopped(pd)) ok = false;
    if (!ok) {
        xx_diskdoubler_stream_free(stream);
        return NULL;
    }
    return stream;
}

/* ------------------------------------------------------------ decoding -- */

/* A fork stored as is, proven by the check its record carries. */
static bool xx_diskdoubler_decode_raw(Abstractformat *self,
                                      const xx_diskdoubler_member *member,
                                      uint8_t **out, size_t *out_size,
                                      xx_pd_struct *pd) {
    uint8_t *plain;
    size_t size = (size_t)member->uncompressed_size;
    size_t index;
    uint32_t check = 0U;

    if (member->compressed_size != member->uncompressed_size) return false;
    plain = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    if (!plain) return false;
    if (size != 0U &&
        !xx_diskdoubler_read_at(self, member->data_offset, plain, size)) {
        xx_mem_free(plain);
        return false;
    }
    if (member->check == XX_DISKDOUBLER_CHECK_XOR8) {
        for (index = 0U; index < size; ++index) check ^= plain[index];
    } else if (member->check == XX_DISKDOUBLER_CHECK_SUM16) {
        for (index = 0U; index < size; ++index) {
            check = (check + plain[index]) & 0xffffU;
        }
    }
    if ((member->check != XX_DISKDOUBLER_CHECK_NONE &&
         check != (uint32_t)member->check_value) ||
        (pd && xx_pd_is_stopped(pd))) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = size;
    return true;
}

/* Extraction of one fork.
 *
 * For a compressed fork the 84-byte file header is re-read here rather than
 * cached in the member struct: the LZW codec needs three header fields (two
 * info bytes and the fork's additive checksum), and the fork's identity --
 * data or resource -- decides which checksum to use. */
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
    if (member->kind == XX_DISKDOUBLER_KIND_RAW) {
        return xx_diskdoubler_decode_raw(self, member, out, out_size, pd);
    }
    if (member->kind != XX_DISKDOUBLER_KIND_DD) return false;

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
        /* Method 8 is Compact Pro's LZH codec verbatim. Two things differ
         * from a .cpt member: a 16-byte preamble precedes the coded stream,
         * and the LZH block size is DiskDoubler's 0xfff0 rather than Compact
         * Pro's 0x1fff0, which is why this calls the generic
         * xx_compactpro_decode_memory rather than the _lzh_ wrapper. */
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

xx_diskdoubler *xx_diskdoubler_create(xx_io_device *device,
                                      int64_t base_address) {
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

int64_t xx_diskdoubler_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd) {
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
         !xx_diskdoubler_set_record(&state->current_record,
                                    &stream->items[0]))) {
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

bool xx_diskdoubler_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    xx_diskdoubler_stream *stream;
    const xx_diskdoubler_member *member;
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

void xx_diskdoubler_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
