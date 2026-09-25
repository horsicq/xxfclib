/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MicroFox PUT archives: PUT.EXE / GET.EXE and the MicroFox Install Program
 * (Jim Hass, MicroFox Company; PUT 2.20's documentation says 1990-1993). PUT
 * writes .PUT files, and .INS files for the installer; both are the same
 * container.
 *
 * The container is LHA's level-0 / level-1 member chain, but PUT stamps its
 * own method tags, so LHA readers (xx_lha included) refuse it:
 *
 *   -lZ0-  stored
 *   -lZ1-  the LHarc -lh1- bitstream: 4 KiB LZSS, adaptive Huffman
 *   -lZ5-  the LHA -lh5- bitstream: 8 KiB LZSS, static Huffman blocks
 *
 * No other tag is accepted, anywhere in the chain. Members sit end to end
 * from the start of the file; the chain ends at a single 0x00 byte where the
 * next header size would be (both corpus archives end with it), or at EOF.
 * One archive may mix the two header levels: one corpus archive stores its
 * -lZ0- member at level 0 between -lZ5- members at level 1; the other is
 * -lZ1- at level 0 throughout.
 *
 * Common part of both levels (offsets from the member start):
 *
 *   0x00  u8      S: header size minus 2 (it counts neither itself nor the
 *                 checksum byte)
 *   0x01  u8      low byte of the sum of header bytes [2, S + 2)
 *   0x02  char[5] method tag, "-lZ0-" / "-lZ1-" / "-lZ5-"
 *   0x07  u32 LE  packed size (level 1: extended headers included)
 *   0x0b  u32 LE  unpacked size
 *   0x0f  u32 LE  MS-DOS time (low word) and date (high word)
 *   0x13  u8      MS-DOS attributes
 *   0x14  u8      header level, 0 or 1
 *   0x15  u8      name length n
 *   0x16  char[n] name, DOS code page 437; '\' separates directories
 *   0x16+n u16 LE CRC-16/ARC of the unpacked data
 *
 * Level 0 ends there (S + 2 >= 24 + n; anything behind the CRC is ignored).
 * Level 1 continues (S + 2 >= 27 + n):
 *
 *   0x18+n u8     originating OS ('M' = MS-DOS)
 *   S     u16 LE  size of the first extended header: the LAST word of the
 *                 base header, whatever sits between it and the OS byte
 *
 * An extended header of size N is N bytes: u8 type, N - 3 bytes of data, and
 * the u16 size of the next one (0 ends the chain). Types used here: 0x00 a
 * CRC-16/ARC of the whole header with its own two bytes taken as zero,
 * checked when present; 0x01 a file name replacing the base one; 0x02 a
 * directory with 0xFF separators. Other types are skipped. The payload starts
 * after the chain and is the packed size minus the chain's bytes.
 *
 * Only the first member has to be well formed. A later header that fails any
 * check ends the archive at that offset, and what follows is overlay: PUT
 * archives travelled through BBS transfers that pad files, and a stored
 * member count does not exist to say whether bytes are missing. Every member
 * that is listed is complete, and extraction verifies its CRC-16.
 *
 * Written from the header layout above and from the observed corpus files;
 * the -lZ1- / -lZ5- = -lh1- / -lh5- mapping is the one Deark (MIT) documents
 * in modules/lha.c. No code was taken from it. The two decoders are the
 * library's own (xxfclib/algo/lzh).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/microfox_put/xx_microfox_put.h"

#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef MICROFOX_PUT
#define XX_MICROFOX_PUT_FILE_TYPE XX_FILE_TYPE_MICROFOX_PUT
#else
#define XX_MICROFOX_PUT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PUT_L0_MIN 24      /* level-0 header with an empty name */
#define PUT_L1_MIN 27      /* level 0 plus the OS byte and the first chain word */
#define PUT_BASE_MAX 257   /* 0xFF + 2 */
#define PUT_EXT_MAX_COUNT 64
#define PUT_HEADER_MAX (PUT_BASE_MAX + 65536)
#define PUT_MAX_MEMBERS 65536
#define PUT_NAME_MAX 1024  /* directory + name, raw bytes */
#define PUT_RENAME_ROOM 48 /* what a duplicate-name suffix may add */
#define PUT_RENAME_PASSES 8
#define PUT_COPY_CHUNK 65536
/* Nothing in the header bounds the unpacked size of a -lZ5- member (one block
 * of zero-length codes expands 7 bytes into 16 MiB), so the allocation is
 * capped outright, as xx_lha caps it. PUT itself is a 16-bit DOS program. */
#define PUT_MAX_DECODED ((int64_t)256 * 1024 * 1024)
/* -lZ1- can do no better than a 60-byte match per 10 bits (a 1-bit symbol
 * and a 9-bit position), i.e. 48 bytes per input byte. */
#define PUT_LZ1_RATIO 64
/* -lZ5- has no such ratio (a one-symbol table codes each symbol in zero
 * bits), but every block costs at least 40 bits of header (16 count, 5 + 3
 * for a one-entry pre-table, 9 for a literal table whose lengths the pre-table
 * codes in zero bits, 4 + 3 for a one-entry position table) and yields at
 * most 65536 symbols of at most 256 bytes. That bounds a short stream. */
#define PUT_LZ5_BLOCK_MIN_BITS 40
#define PUT_LZ5_BLOCK_MAX_OUT ((int64_t)65536 * 256)

typedef struct put_member_s {
    uint8_t *raw;  /* NUL-terminated, '/'-separated, code page 437 bytes */
    uint8_t *orig; /* the name as stored, once raw has been renamed */
    char *name;    /* UTF-8, filled when records are read */
    int64_t header_offset; /* absolute */
    int64_t header_size;
    int64_t data_offset;   /* absolute */
    int64_t packed_size;
    int64_t unpacked_size;
    uint32_t dos_datetime;
    uint16_t crc16;
    uint8_t method; /* '0', '1' or '5' */
    uint8_t level;
    uint8_t attributes;
    bool extractable;
} put_member;

typedef struct put_stream_s {
    put_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size; /* relative to base_address */
} put_stream;

static void put_vtable_destroy(Abstractformat *self);

/* -------------------------------------------------------------- helpers -- */

static uint16_t put_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t put_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool put_read_at(xx_io_device *device, int64_t offset, uint8_t *buffer,
                        size_t size) {
    size_t done = 0U;

    if (!device || offset < 0 || (!buffer && size != 0U) ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* CRC-16/ARC (reflected 0x8005), four bits at a time. */
static const uint16_t put_crc_nibble[16] = {
    0x0000U, 0xCC01U, 0xD801U, 0x1400U, 0xF001U, 0x3C00U, 0x2800U, 0xE401U,
    0xA001U, 0x6C00U, 0x7800U, 0xB401U, 0x5000U, 0x9C01U, 0x8801U, 0x4400U};

static uint16_t put_crc16_update(uint16_t crc, const uint8_t *data,
                                 size_t size) {
    size_t index;

    for (index = 0U; index < size; ++index) {
        uint8_t byte = data[index];
        crc = (uint16_t)((crc >> 4) ^ put_crc_nibble[(crc ^ byte) & 0x0FU]);
        crc = (uint16_t)((crc >> 4) ^
                         put_crc_nibble[(crc ^ (byte >> 4)) & 0x0FU]);
    }
    return crc;
}

/* The header CRC of extended type 0 counts its own two bytes as zero. */
static uint16_t put_header_crc(const uint8_t *header, size_t size,
                               size_t skip) {
    static const uint8_t zero[2] = {0U, 0U};
    uint16_t crc = put_crc16_update(0U, header, skip);

    crc = put_crc16_update(crc, zero, 2U);
    return put_crc16_update(crc, header + skip + 2U, size - skip - 2U);
}

static bool put_tag_ok(const uint8_t *header) {
    return header[2] == (uint8_t)'-' && header[3] == (uint8_t)'l' &&
           header[4] == (uint8_t)'Z' &&
           (header[5] == (uint8_t)'0' || header[5] == (uint8_t)'1' ||
            header[5] == (uint8_t)'5') &&
           header[6] == (uint8_t)'-';
}

/* Names are raw code page bytes; a control byte in one means the walk has
 * wandered into payload. */
static bool put_name_byte_ok(uint8_t byte) {
    return byte >= 0x20U && byte != 0x7FU;
}

static void put_stream_free(void *pointer) {
    put_stream *stream = (put_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_mem_free(stream->items[index].raw);
        xx_mem_free(stream->items[index].orig);
        xx_mem_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool put_stream_add(put_stream *stream, const put_member *member) {
    if (stream->count == stream->capacity) {
        size_t grown_capacity = stream->capacity ? stream->capacity * 2U : 16U;
        put_member *grown;

        if (grown_capacity > (size_t)PUT_MAX_MEMBERS) {
            grown_capacity = (size_t)PUT_MAX_MEMBERS;
        }
        if (grown_capacity <= stream->capacity) return false;
        grown = (put_member *)xx_mem_realloc(
            stream->items, grown_capacity * sizeof(*grown));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = grown_capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* ---------------------------------------------------------------- parse -- */

/* Append @p size bytes of a name or directory field to @p out, turning the
 * separators into '/'. A NUL ends a name field early (the bytes after it are
 * still covered by the header checksum, but are not the name). */
static bool put_append_field(uint8_t *out, size_t *used, const uint8_t *field,
                             int32_t size, bool is_dir) {
    int32_t index;

    for (index = 0; index < size; ++index) {
        uint8_t byte = field[index];

        if (byte == 0U) {
            if (is_dir) return false;
            break;
        }
        if (byte == (uint8_t)'\\' || (is_dir && byte == 0xFFU)) {
            byte = (uint8_t)'/';
        }
        if (!put_name_byte_ok(byte)) return false;
        if (*used >= (size_t)PUT_NAME_MAX) return false;
        out[(*used)++] = byte;
    }
    return true;
}

/* Parse the member at @p offset (relative to base_address). @p header needs
 * PUT_HEADER_MAX bytes, @p name PUT_NAME_MAX + 1. On success the member's
 * raw name is left NUL-terminated in @p name and not yet owned by
 * @p member. */
static bool put_parse_member(Abstractformat *self, uint8_t *header,
                             uint8_t *name, int64_t offset, int64_t span,
                             put_member *member, xx_pd_struct *pd) {
    xx_io_device *device = self->device;
    int64_t absolute = self->base_address + offset;
    int64_t remaining = span - offset;
    int64_t packed;
    int64_t unpacked;
    int64_t ext_total = 0;
    int32_t base_size;
    int32_t header_total;
    int32_t min_base;
    int32_t name_length;
    int32_t name_pos = 22;
    int32_t name_size;
    int32_t dir_pos = -1;
    int32_t dir_size = 0;
    int32_t crc_pos = -1;
    int32_t index;
    uint32_t sum = 0U;
    uint8_t level;
    size_t used = 0U;

    if (remaining < PUT_L0_MIN) return false;
    if (!put_read_at(device, absolute, header, 1U)) return false;
    base_size = (int32_t)header[0] + 2;
    if (base_size < PUT_L0_MIN || (int64_t)base_size > remaining) return false;
    if (!put_read_at(device, absolute, header, (size_t)base_size)) return false;
    if (!put_tag_ok(header)) return false;
    level = header[20];
    if (level > 1U) return false;
    for (index = 2; index < base_size; ++index) sum += header[index];
    if ((sum & 0xFFU) != (uint32_t)header[1]) return false;

    name_length = (int32_t)header[21];
    min_base = (level == 0U) ? PUT_L0_MIN : PUT_L1_MIN;
    if (min_base + name_length > base_size) return false;
    name_size = name_length;
    packed = (int64_t)put_le32(header + 7);
    unpacked = (int64_t)put_le32(header + 11);
    header_total = base_size;

    if (level == 1U) {
        int32_t next = (int32_t)put_le16(header + base_size - 2);
        int ext_count = 0;

        while (next != 0) {
            int32_t data_pos;
            int32_t data_size;
            uint8_t type;

            if (pd && xx_pd_is_stopped(pd)) return false;
            if (++ext_count > PUT_EXT_MAX_COUNT) return false;
            /* type byte plus the next size word */
            if (next < 3) return false;
            if (next > PUT_HEADER_MAX - header_total) return false;
            /* the chain is counted inside the packed size */
            if ((int64_t)next > packed - ext_total) return false;
            if ((int64_t)next > remaining - (int64_t)header_total) return false;
            if (!put_read_at(device, absolute + header_total,
                             header + header_total, (size_t)next)) {
                return false;
            }
            type = header[header_total];
            data_pos = header_total + 1;
            data_size = next - 3;
            if (type == 0x00U) {
                /* Two header CRCs would leave "which one counts" open. */
                if (data_size < 2 || crc_pos >= 0) return false;
                crc_pos = data_pos;
            } else if (type == 0x01U) {
                name_pos = data_pos;
                name_size = data_size;
            } else if (type == 0x02U) {
                dir_pos = data_pos;
                dir_size = data_size;
            }
            header_total += next;
            ext_total += next;
            next = (int32_t)put_le16(header + header_total - 2);
        }
        if (crc_pos >= 0 &&
            put_header_crc(header, (size_t)header_total, (size_t)crc_pos) !=
                put_le16(header + crc_pos)) {
            return false;
        }
        packed -= ext_total;
    }

    if (packed < 0 || unpacked < 0) return false;
    /* A stored member states the same length twice. */
    if (header[5] == (uint8_t)'0' && packed != unpacked) return false;
    if (packed > remaining - (int64_t)header_total) return false;

    if (dir_size > 0) {
        if (!put_append_field(name, &used, header + dir_pos, dir_size, true)) {
            return false;
        }
        if (used > 0U && name[used - 1U] != (uint8_t)'/') {
            if (used >= (size_t)PUT_NAME_MAX) return false;
            name[used++] = (uint8_t)'/';
        }
    }
    if (!put_append_field(name, &used, header + name_pos, name_size, false)) {
        return false;
    }
    if (used == 0U) return false;
    name[used] = 0U;

    xx_mem_zero(member, sizeof(*member));
    member->header_offset = absolute;
    member->header_size = header_total;
    member->data_offset = absolute + header_total;
    member->packed_size = packed;
    member->unpacked_size = unpacked;
    member->dos_datetime = put_le32(header + 15);
    member->crc16 = put_le16(header + 22 + name_length);
    member->method = header[5];
    member->level = level;
    member->attributes = header[19];
    member->extractable = true;
    return true;
}

static put_stream *put_parse(Abstractformat *self, xx_pd_struct *pd) {
    put_stream *stream = NULL;
    uint8_t *header = NULL;
    uint8_t *name = NULL;
    int64_t total;
    int64_t span;
    int64_t offset = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < PUT_L0_MIN) return NULL;

    stream = (put_stream *)xx_mem_calloc(1U, sizeof(*stream));
    header = (uint8_t *)xx_mem_alloc((size_t)PUT_HEADER_MAX);
    name = (uint8_t *)xx_mem_alloc((size_t)PUT_NAME_MAX + 1U);
    if (!stream || !header || !name) goto fail;

    for (;;) {
        put_member member;
        uint8_t first;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (offset >= span) {
            stream->archive_size = span;
            break;
        }
        if (!put_read_at(self->device, self->base_address + offset, &first,
                         1U)) {
            goto fail;
        }
        if (first == 0U && stream->count > 0U) {
            /* The end marker belongs to the archive; anything after it is
             * overlay. */
            stream->archive_size = offset + 1;
            break;
        }
        if (stream->count >= (size_t)PUT_MAX_MEMBERS ||
            !put_parse_member(self, header, name, offset, span, &member, pd)) {
            if (stream->count == 0U) goto fail;
            if (pd && xx_pd_is_stopped(pd)) goto fail;
            stream->archive_size = offset;
            break;
        }
        member.raw = (uint8_t *)xx_str_dup((const char *)name);
        if (!member.raw) goto fail;
        if (!put_stream_add(stream, &member)) {
            xx_str_free((char *)member.raw);
            goto fail;
        }
        offset = member.data_offset - self->base_address + member.packed_size;
    }

    xx_mem_free(header);
    xx_mem_free(name);
    return stream;

fail:
    xx_mem_free(header);
    xx_mem_free(name);
    put_stream_free(stream);
    return NULL;
}

/* ---------------------------------------------------------------- names -- */

/* Code page 437, 0x80..0xFF, as Unicode. */
static const uint16_t put_cp437_high[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static char *put_to_utf8(const uint8_t *raw) {
    size_t length = xx_str_len((const char *)raw);
    size_t out = 0U;
    size_t index;
    char *text;

    if (length > (size_t)(PUT_NAME_MAX + PUT_RENAME_ROOM)) return NULL;
    text = (char *)xx_mem_alloc(length * 3U + 1U);
    if (!text) return NULL;
    for (index = 0U; index < length; ++index) {
        uint32_t code = raw[index] < 0x80U ? (uint32_t)raw[index]
                                           : put_cp437_high[raw[index] - 0x80U];
        if (code < 0x80U) {
            text[out++] = (char)code;
        } else if (code < 0x800U) {
            text[out++] = (char)(0xC0U | (code >> 6));
            text[out++] = (char)(0x80U | (code & 0x3FU));
        } else {
            text[out++] = (char)(0xE0U | (code >> 12));
            text[out++] = (char)(0x80U | ((code >> 6) & 0x3FU));
            text[out++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    text[out] = '\0';
    return text;
}

/* The case folding Windows file systems apply, restricted to what code page
 * 437 can spell: ASCII letters and the eight accented pairs, plus sigma. */
static uint8_t put_fold(uint8_t byte) {
    if (byte >= (uint8_t)'a' && byte <= (uint8_t)'z') {
        return (uint8_t)(byte - 0x20U);
    }
    switch (byte) {
        case 0x81U: return 0x9AU; /* u umlaut */
        case 0x82U: return 0x90U; /* e acute */
        case 0x84U: return 0x8EU; /* a umlaut */
        case 0x86U: return 0x8FU; /* a ring */
        case 0x87U: return 0x80U; /* c cedilla */
        case 0x91U: return 0x92U; /* ae */
        case 0x94U: return 0x99U; /* o umlaut */
        case 0xA4U: return 0xA5U; /* n tilde */
        case 0xE5U: return 0xE4U; /* sigma */
        default: return byte;
    }
}

static int put_name_compare(const uint8_t *left, const uint8_t *right) {
    for (;;) {
        uint8_t a = put_fold(*left++);
        uint8_t b = put_fold(*right++);

        if (a != b) return a < b ? -1 : 1;
        if (a == 0U) return 0;
    }
}

/* Order by folded name, then members still carrying their stored name before
 * renamed ones, then by member index. The first member of a group of equal
 * names is the one that keeps its name: an original name wins over one this
 * reader made up, and among originals the earliest member wins. */
static bool put_order_less(const put_stream *stream, size_t a, size_t b) {
    const put_member *left = &stream->items[a];
    const put_member *right = &stream->items[b];
    int order = put_name_compare(left->raw, right->raw);

    if (order != 0) return order < 0;
    if ((left->orig != NULL) != (right->orig != NULL)) {
        return left->orig == NULL;
    }
    return a < b;
}

static void put_sift_down(const put_stream *stream, size_t *order,
                          size_t root, size_t size) {
    for (;;) {
        size_t child = root * 2U + 1U;
        size_t swap;

        if (child >= size) return;
        if (child + 1U < size &&
            put_order_less(stream, order[child], order[child + 1U])) {
            ++child;
        }
        if (!put_order_less(stream, order[root], order[child])) return;
        swap = order[root];
        order[root] = order[child];
        order[child] = swap;
        root = child;
    }
}

/* Heapsort: O(n log n) whatever the names, so a crafted archive cannot make
 * this quadratic. */
static void put_sort(const put_stream *stream, size_t *order, size_t count) {
    size_t index;

    for (index = 0U; index < count; ++index) order[index] = index;
    if (count < 2U) return;
    for (index = count / 2U; index-- > 0U;) {
        put_sift_down(stream, order, index, count);
    }
    for (index = count - 1U; index > 0U; --index) {
        size_t swap = order[0];
        order[0] = order[index];
        order[index] = swap;
        put_sift_down(stream, order, 0U, index);
    }
}

static size_t put_format_decimal(char *out, size_t value) {
    char digits[24];
    size_t count = 0U;
    size_t index;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    for (index = 0U; index < count; ++index) {
        out[index] = digits[count - 1U - index];
    }
    return count;
}

/* Stored "DIR/NAME.EXT" -> "DIR/NAME_<n>.EXT" (or "_<n>_<pass>" on later
 * passes), always built from the stored name, never from an earlier rename.
 * Returns 1 on success, 0 when the result would be too long, -1 when out of
 * memory. */
static int put_rename(put_member *member, size_t number, unsigned pass) {
    char suffix[PUT_RENAME_ROOM];
    const uint8_t *source = member->orig ? member->orig : member->raw;
    size_t suffix_length = 0U;
    size_t length = xx_str_len((const char *)source);
    size_t component = 0U;
    size_t insert;
    size_t index;
    uint8_t *renamed;

    suffix[suffix_length++] = '_';
    suffix_length += put_format_decimal(suffix + suffix_length, number);
    if (pass > 1U) {
        suffix[suffix_length++] = '_';
        suffix_length += put_format_decimal(suffix + suffix_length, pass);
    }
    if (length + suffix_length > (size_t)(PUT_NAME_MAX + PUT_RENAME_ROOM)) {
        return 0;
    }
    for (index = 0U; index < length; ++index) {
        if (source[index] == (uint8_t)'/') component = index + 1U;
    }
    insert = length;
    for (index = length; index > component + 1U; --index) {
        if (source[index - 1U] == (uint8_t)'.') {
            insert = index - 1U;
            break;
        }
    }
    renamed = (uint8_t *)xx_mem_alloc(length + suffix_length + 1U);
    if (!renamed) return -1;
    xx_rt_memcpy(renamed, source, insert);
    xx_rt_memcpy(renamed + insert, suffix, suffix_length);
    xx_rt_memcpy(renamed + insert + suffix_length, source + insert,
                 length - insert);
    renamed[length + suffix_length] = 0U;
    if (member->orig) {
        xx_mem_free(member->raw);
    } else {
        member->orig = member->raw;
    }
    member->raw = renamed;
    return 1;
}

/* Give every member a name no other member shares, even case-insensitively,
 * so that extracting one can never replace another. In each group of equal
 * names the first member in put_order_less() order keeps its name and every
 * other one gets "_<member number>" before its extension; if that collides
 * in turn (with a stored name, say), the next pass renames the made-up one
 * again with the pass number added. Whatever is still shared after the last
 * pass is marked as not extractable. */
static bool put_make_unique(put_stream *stream) {
    size_t *order;
    unsigned pass;

    if (stream->count < 2U) return true;
    order = (size_t *)xx_mem_alloc(stream->count * sizeof(*order));
    if (!order) return false;
    for (pass = 1U; pass <= PUT_RENAME_PASSES + 1U; ++pass) {
        bool changed = false;
        size_t keeper = 0U;
        size_t index;

        put_sort(stream, order, stream->count);
        keeper = order[0];
        for (index = 1U; index < stream->count; ++index) {
            put_member *later = &stream->items[order[index]];

            /* Compare with the group's keeper, which this pass never
             * renames, not with the previous entry, which it may have. */
            if (put_name_compare(stream->items[keeper].raw, later->raw) != 0) {
                keeper = order[index];
                continue;
            }
            if (pass > PUT_RENAME_PASSES) {
                later->extractable = false;
                continue;
            }
            {
                int renamed = put_rename(later, order[index] + 1U, pass);
                if (renamed < 0) {
                    xx_mem_free(order);
                    return false;
                }
                if (renamed == 0) later->extractable = false;
            }
            changed = true;
        }
        if (!changed) break;
    }
    xx_mem_free(order);
    return true;
}

static bool put_is_word(const uint8_t *text, size_t length, const char *word) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        if (word[index] == '\0' || put_fold(text[index]) != (uint8_t)word[index]) {
            return false;
        }
    }
    return word[length] == '\0';
}

/* A component Windows would treat as a device, with or without extension:
 * CON, PRN, AUX, NUL, CONIN$, CONOUT$, CLOCK$, COM0-9, LPT0-9 (and COM/LPT
 * with the superscript two, 0xFD in code page 437). */
static bool put_is_device(const uint8_t *component, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U;
    size_t index;

    while (stem < length && component[stem] != (uint8_t)'.') ++stem;
    while (stem > 0U && component[stem - 1U] == (uint8_t)' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        if (put_is_word(component, stem, devices[index])) return true;
    }
    if (stem == 4U && ((component[3] >= (uint8_t)'0' &&
                        component[3] <= (uint8_t)'9') ||
                       component[3] == 0xFDU)) {
        return put_is_word(component, 3U, "COM") ||
               put_is_word(component, 3U, "LPT");
    }
    return false;
}

/* Extraction writes <base>/<name>. Refused: an absolute name, empty, "." and
 * ".." components, a component ending in '.' or ' ' (Windows drops those,
 * so it would alias another name), drive and stream colons and the other
 * characters Windows reserves, and device names. */
static bool put_name_safe(const uint8_t *raw) {
    size_t start = 0U;

    if (!raw || raw[0] == 0U || raw[0] == (uint8_t)'/') return false;
    for (;;) {
        size_t end = start;
        size_t index;

        while (raw[end] != 0U && raw[end] != (uint8_t)'/') ++end;
        if (end == start) return false;
        if (raw[end - 1U] == (uint8_t)'.' || raw[end - 1U] == (uint8_t)' ') {
            return false; /* also catches "." and ".." */
        }
        for (index = start; index < end; ++index) {
            uint8_t byte = raw[index];

            if (!put_name_byte_ok(byte) || byte == (uint8_t)':' ||
                byte == (uint8_t)'<' || byte == (uint8_t)'>' ||
                byte == (uint8_t)'"' || byte == (uint8_t)'|' ||
                byte == (uint8_t)'?' || byte == (uint8_t)'*') {
                return false;
            }
        }
        if (put_is_device(raw + start, end - start)) return false;
        if (raw[end] == 0U) return true;
        start = end + 1U;
    }
}

/* --------------------------------------------------------------- decode -- */

typedef struct put_sink_s {
    xx_io_device *device; /* NULL: verify only */
    uint16_t crc;
} put_sink;

static bool put_sink_write(put_sink *sink, const uint8_t *data, size_t size) {
    size_t done = 0U;

    sink->crc = put_crc16_update(sink->crc, data, size);
    if (!sink->device) return true;
    while (done < size) {
        ssize_t sent = xx_io_write(sink->device, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) return false;
        done += (size_t)sent;
    }
    return true;
}

/* Stored members are copied through a fixed buffer; packed ones are decoded
 * in memory, with the CRC checked BEFORE anything is written. */
static bool put_extract(Abstractformat *self, const put_member *member,
                        xx_io_device *destination, xx_pd_struct *pd) {
    put_sink sink;
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    bool ok = false;

    sink.device = destination;
    sink.crc = 0U;
    if (member->packed_size < 0 || member->unpacked_size < 0) return false;

    if (member->method == (uint8_t)'0') {
        int64_t left = member->packed_size;
        int64_t position = member->data_offset;
        uint8_t *chunk;

        if (member->packed_size != member->unpacked_size) return false;
        chunk = (uint8_t *)xx_mem_alloc((size_t)PUT_COPY_CHUNK);
        if (!chunk) return false;
        ok = true;
        while (ok && left > 0) {
            size_t piece = left > (int64_t)PUT_COPY_CHUNK ? (size_t)PUT_COPY_CHUNK
                                                          : (size_t)left;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !put_read_at(self->device, position, chunk, piece) ||
                !put_sink_write(&sink, chunk, piece)) {
                ok = false;
                break;
            }
            position += (int64_t)piece;
            left -= (int64_t)piece;
        }
        xx_mem_free(chunk);
        return ok && sink.crc == member->crc16;
    }

    if (member->unpacked_size > PUT_MAX_DECODED ||
        member->packed_size > PUT_MAX_DECODED) {
        return false;
    }
    if (member->method == (uint8_t)'1' &&
        member->unpacked_size >
            member->packed_size * PUT_LZ1_RATIO + PUT_LZ1_RATIO) {
        return false;
    }
    if (member->method == (uint8_t)'5' &&
        member->unpacked_size >
            (member->packed_size * 8 / PUT_LZ5_BLOCK_MIN_BITS + 1) *
                PUT_LZ5_BLOCK_MAX_OUT) {
        return false;
    }
    if (member->unpacked_size == 0) {
        /* Nothing to decode; the CRC of no bytes is 0. */
        return member->crc16 == 0U;
    }
    if (member->packed_size == 0) return false;

    packed = (uint8_t *)xx_mem_alloc((size_t)member->packed_size);
    plain = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!packed || !plain) goto done;
    if (!put_read_at(self->device, member->data_offset, packed,
                     (size_t)member->packed_size)) {
        goto done;
    }
    if (pd && xx_pd_is_stopped(pd)) goto done;
    if (member->method == (uint8_t)'1') {
        if (!xx_lzh1_decode_memory(packed, (size_t)member->packed_size, plain,
                                   (size_t)member->unpacked_size, &written)) {
            goto done;
        }
    } else if (member->method == (uint8_t)'5') {
        if (!xx_lzh5_decode_memory(packed, (size_t)member->packed_size, plain,
                                   (size_t)member->unpacked_size, 5,
                                   &written)) {
            goto done;
        }
    } else {
        goto done;
    }
    if (written != (size_t)member->unpacked_size) goto done;
    if (put_crc16_update(0U, plain, written) != member->crc16) goto done;
    if (pd && xx_pd_is_stopped(pd)) goto done;
    ok = put_sink_write(&sink, plain, written);

done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return ok;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_microfox_put_init(xx_microfox_put *archive, xx_io_device *device,
                          int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MICROFOX_PUT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-microfox-put");
    xx_format_set_extension(&archive->format, "put");
    archive->format.check_is_valid = xx_microfox_put_check_is_valid;
    archive->format.handle_base_info = xx_microfox_put_handle_base_info;
    archive->format.get_format_size = xx_microfox_put_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_microfox_put_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_microfox_put_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_microfox_put_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_microfox_put_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_microfox_put_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_microfox_put_free_archive_records_reading;
    archive->format.destroy = put_vtable_destroy;
}

xx_microfox_put *xx_microfox_put_create(xx_io_device *device,
                                        int64_t base_address) {
    xx_microfox_put *archive =
        (xx_microfox_put *)xx_mem_alloc(sizeof(*archive));

    if (archive) xx_microfox_put_init(archive, device, base_address);
    return archive;
}

void xx_microfox_put_destroy(xx_microfox_put *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: that dispatches through format.destroy, which
     * is the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_microfox_put_free(xx_microfox_put *archive) {
    if (!archive) return;
    xx_microfox_put_destroy(archive);
    xx_mem_free(archive);
}

static void put_vtable_destroy(Abstractformat *self) {
    xx_microfox_put_destroy((xx_microfox_put *)self);
}

/* --------------------------------------------------------------- format -- */

bool xx_microfox_put_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    put_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = put_parse(self, pd);
    if (!stream) return false;
    put_stream_free(stream);
    return true;
}

bool xx_microfox_put_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_microfox_put *archive = (xx_microfox_put *)self;
    put_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = put_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    put_stream_free(stream);
    return true;
}

int64_t xx_microfox_put_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_microfox_put_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_microfox_put *)self)->number_of_records : 0U;
}

/* -------------------------------------------------------------- records -- */

static bool put_set_record(xx_archive_record *record,
                           const put_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->unpacked_size) &&
           /* The three tag characters, packed big-endian: "lZ5" = 0x6C5A35. */
           xx_archive_record_set_meta_u64(
               record, XX_META_ID_COMPRESSION_METHOD,
               ((uint64_t)'l' << 16) | ((uint64_t)'Z' << 8) |
                   (uint64_t)member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_datetime & 0xFFFFU) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_datetime >> 16) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool put_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;

    if (!options) return true;
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

xx_archive_record_state *xx_microfox_put_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    put_stream *stream;
    xx_archive_record_state *state;
    size_t index;

    if (!self || !self->device) return NULL;
    stream = put_parse(self, pd);
    if (!stream) return NULL;
    if (!put_make_unique(stream)) {
        put_stream_free(stream);
        return NULL;
    }
    for (index = 0U; index < stream->count; ++index) {
        stream->items[index].name = put_to_utf8(stream->items[index].raw);
        if (!stream->items[index].name) {
            put_stream_free(stream);
            return NULL;
        }
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        put_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = put_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!put_copy_options(&state->options, options) ||
        !put_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_microfox_put_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_microfox_put_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    put_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (put_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        put_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_microfox_put_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    put_stream *stream;
    const put_member *member;
    const xx_var *option;
    const char *base_path = NULL;
    char *converted = NULL;
    char *target = NULL;
    xx_io_device *output;
    size_t base_length;
    bool overwrite;
    bool result;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (put_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];

    option = xx_format_resolve_extra_parameter(self, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->unpacked_size > xx_var_get_u64(option)) {
        return false;
    }
    option = xx_format_resolve_extra_parameter(self, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: decode and discard, which verifies the member. */
        return put_extract(self, member, NULL, pd);
    }
    if (!member->extractable || !put_name_safe(member->raw)) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base_path = converted;
    }
    if (!base_path) {
        xx_str_free(converted);
        return false;
    }
    base_length = xx_str_len(base_path);
    if (base_length != 0U && base_path[base_length - 1U] != '/' &&
        base_path[base_length - 1U] != '\\') {
        target = xx_str_concat3(base_path, "/", member->name);
    } else {
        target = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted);
    if (!target) return false;
    if (!xx_store_create_dirs_a(target, false)) {
        xx_str_free(target);
        return false;
    }

    option = xx_format_resolve_extra_parameter(self, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    /* Without the overwrite option an existing file is never replaced --
     * whatever produced it, an earlier member included (a name Windows
     * resolves to the same file through an 8.3 alias, say). */
    output = xx_io_file_open(target, overwrite ? "wb" : "wbx");
    if (!output) {
        xx_str_free(target);
        return false;
    }
    result = put_extract(self, member, output, pd);
    if (xx_io_close(output) != 0) result = false;
    if (!result) xx_rt_remove(target);
    xx_str_free(target);
    return result;
}

void xx_microfox_put_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
