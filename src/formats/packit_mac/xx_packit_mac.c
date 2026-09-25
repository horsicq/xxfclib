/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Macintosh PackIt archives (.pit).  xx_packit_mac.h carries the field
 * table.
 *
 * The member walk and the Huffman stream layout follow Deark's
 * modules/packit.c and the "PackIt/StuffIt"-style decoder in
 * src/fmtutil-cmpr.c (Deark 1.7.3, Copyright (C) 2016-2026 Jason Summers,
 * MIT license); the code here is an independent implementation of that
 * structure, with its own iterative tree builder and bounded decoder.
 *
 * Decoding is streamed: nothing proportional to a fork's declared length is
 * ever allocated.  A Huffman member has no stored length, so the walk has to
 * decode each one to learn where the next member starts; the decoder is
 * bounded by the bytes actually present (every code costs at least one bit,
 * and a member that asks for more output than its remaining input can carry
 * is refused before decoding starts).  Both CRC-16/XMODEM values are hard
 * gates at extraction: the body header CRC before anything is written, and
 * the fork CRC once both forks have streamed past (a failing output file is
 * removed again).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/packit_mac/xx_packit_mac.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as PACKIT_MAC is registered. */
#ifdef PACKIT_MAC
#define XX_PACKIT_MAC_FILE_TYPE XX_FILE_TYPE_PACKIT_MAC
#else
#define XX_PACKIT_MAC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PIT_MARKER_SIZE 4
#define PIT_BODY_HEADER 94U
#define PIT_HEADER_CRC_SPAN 92U
#define PIT_TRAILER 2U
#define PIT_NAME_FIELD 63U
#define PIT_STORED_OVERHEAD (PIT_MARKER_SIZE + PIT_BODY_HEADER + PIT_TRAILER)

#define PIT_OFF_NAME_LENGTH 0U
#define PIT_OFF_NAME 1U
#define PIT_OFF_TYPE 64U
#define PIT_OFF_CREATOR 68U
#define PIT_OFF_FINDER 72U
#define PIT_OFF_DATA_LENGTH 76U
#define PIT_OFF_RSRC_LENGTH 80U
#define PIT_OFF_MODIFIED 88U
#define PIT_OFF_HEADER_CRC 92U

/* Archives of the PackIt era hold a handful of members; this only stops a
 * crafted chain of tiny members from growing the tables without bound. */
#define PIT_MAX_MEMBERS 16384U
#define PIT_MAX_FORK UINT32_C(0x7FFFFFFF)

/* A code tree has at most 256 leaves, so at most 255 internal nodes, and no
 * code is longer than 255 bits. */
#define PIT_MAX_INTERNAL 255U
#define PIT_LEAF 0x8000U

#define PIT_INPUT_BUFFER 4096U
#define PIT_CHUNK 4096U

/* 63 Mac Roman bytes are at most 189 UTF-8 bytes; room for "_<n>" and
 * ".rsrc" on top. */
#define PIT_NAME_BUFFER 256U
#define PIT_ENCRYPTED_NAME "encrypted_"

#define PIT_KIND_STORED 0U
#define PIT_KIND_HUFFMAN 1U
#define PIT_KIND_ENCRYPTED 2U

typedef struct pit_member_s {
    int64_t offset;   /**< Absolute offset of the member's marker. */
    int64_t size;     /**< Marker plus body (or stream) bytes. */
    uint32_t data_length;
    uint32_t rsrc_length;
    uint32_t type;
    uint32_t modified;
    uint16_t finder_flags;
    uint8_t name_length;
    uint8_t name[PIT_NAME_FIELD];
    uint8_t kind;
    uint8_t marker;   /**< Fourth marker byte. */
} pit_member;

typedef struct pit_item_s {
    char *name;
    size_t member;
    bool resource;
} pit_item;

typedef struct pit_stream_s {
    pit_member *members;
    size_t member_count;
    size_t member_capacity;
    pit_item *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    bool end_marker;
    bool encrypted;
} pit_stream;

typedef struct pit_reader_s {
    xx_io_device *device;  /**< NULL when reading from memory. */
    const uint8_t *data;
    int64_t next;          /**< Device offset of the next refill. */
    int64_t limit;         /**< Device offset input must not reach. */
    uint64_t fetched;      /**< Bytes handed to the bit reader / caller. */
    size_t length;
    size_t position;
    uint32_t bits;
    uint32_t bit_count;
    bool huffman;
    uint16_t tree[PIT_MAX_INTERNAL][2];
    uint8_t buffer[PIT_INPUT_BUFFER];
    uint8_t chunk[PIT_CHUNK];
} pit_reader;

typedef enum pit_step_e {
    PIT_STEP_MEMBER,
    PIT_STEP_END,
    PIT_STEP_ENCRYPTED,
    PIT_STEP_STOP
} pit_step;

/* Mac Roman 0x80..0xFF as Unicode code points. */
static const uint16_t pit_mac_roman[128] = {
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
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

static uint32_t pit_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint16_t pit_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint32_t)bytes[0] << 8U) | (uint32_t)bytes[1]);
}

static bool pit_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Input: bytes, bits, Huffman codes                                       */

static void pit_reader_start(pit_reader *r, xx_io_device *device,
                             const uint8_t *memory, int64_t offset,
                             int64_t limit, bool huffman) {
    r->device = device;
    r->data = memory;
    r->next = offset;
    r->limit = limit;
    r->fetched = 0U;
    r->length = memory ? (size_t)limit : 0U;
    r->position = 0U;
    r->bits = 0U;
    r->bit_count = 0U;
    r->huffman = huffman;
}

static bool pit_fetch(pit_reader *r, uint8_t *byte) {
    if (r->position >= r->length) {
        int64_t left;
        size_t amount;
        if (!r->device) return false;
        left = r->limit - r->next;
        if (left <= 0) return false;
        amount = left < (int64_t)sizeof(r->buffer) ? (size_t)left
                                                   : sizeof(r->buffer);
        if (!pit_read_at(r->device, r->next, r->buffer, amount)) return false;
        r->data = r->buffer;
        r->next += (int64_t)amount;
        r->length = amount;
        r->position = 0U;
    }
    *byte = r->data[r->position++];
    ++r->fetched;
    return true;
}

/* Input bits not yet consumed. */
static uint64_t pit_bits_left(const pit_reader *r) {
    uint64_t bytes = (uint64_t)(r->length - r->position);
    if (r->device && r->limit > r->next)
        bytes += (uint64_t)(r->limit - r->next);
    return bytes * 8U + r->bit_count;
}

/* One bit, most significant first; -1 at the end of the input. */
static int pit_bit(pit_reader *r) {
    if (r->bit_count == 0U) {
        uint8_t byte;
        if (!pit_fetch(r, &byte)) return -1;
        r->bits = byte;
        r->bit_count = 8U;
    }
    --r->bit_count;
    return (int)((r->bits >> r->bit_count) & 1U);
}

/* The tree is written in pre-order: 1 and an 8-bit value is a leaf, 0 is an
 * internal node whose left (code bit 0) subtree follows first.  Slots still
 * to be filled sit on an explicit stack (slot = node * 2 + side), so a
 * hostile tree cannot recurse; each internal node adds one net slot, which
 * bounds the stack by the node limit.  A root that is itself a leaf would
 * give zero-length codes and is refused, as the reference refuses it. */
static bool pit_read_tree(pit_reader *r) {
    uint16_t stack[PIT_MAX_INTERNAL + 2U];
    size_t depth = 0U;
    uint32_t internal = 1U;
    if (pit_bit(r) != 0) return false;
    stack[depth++] = 1U;
    stack[depth++] = 0U;
    while (depth != 0U) {
        uint16_t slot = stack[--depth];
        int bit = pit_bit(r);
        if (bit < 0) return false;
        if (bit) {
            uint32_t value = 0U, index;
            for (index = 0U; index < 8U; ++index) {
                int next = pit_bit(r);
                if (next < 0) return false;
                value = (value << 1U) | (uint32_t)next;
            }
            r->tree[slot >> 1U][slot & 1U] = (uint16_t)(PIT_LEAF | value);
        } else {
            uint16_t node;
            if (internal >= PIT_MAX_INTERNAL ||
                depth + 2U > sizeof(stack) / sizeof(stack[0]))
                return false;
            node = (uint16_t)internal++;
            r->tree[slot >> 1U][slot & 1U] = node;
            stack[depth++] = (uint16_t)(node * 2U + 1U);
            stack[depth++] = (uint16_t)(node * 2U);
        }
    }
    return true;
}

/* Every child slot was filled by pit_read_tree and every internal child has
 * a larger index than its parent, so a code ends within PIT_MAX_INTERNAL
 * bits; the step bound only restates that. */
static bool pit_symbol(pit_reader *r, uint8_t *out) {
    uint32_t node = 0U, steps;
    for (steps = 0U; steps < PIT_MAX_INTERNAL; ++steps) {
        int bit = pit_bit(r);
        uint16_t child;
        if (bit < 0) return false;
        child = r->tree[node][bit];
        if ((child & PIT_LEAF) != 0U) {
            *out = (uint8_t)child;
            return true;
        }
        if (child <= node || child >= PIT_MAX_INTERNAL) return false;
        node = child;
    }
    return false;
}

static bool pit_next(pit_reader *r, uint8_t *out) {
    return r->huffman ? pit_symbol(r, out) : pit_fetch(r, out);
}

static bool pit_read_body(pit_reader *r, uint8_t *out, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index)
        if (!pit_next(r, out + index)) return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member walk                                                             */

static bool pit_body_header_valid(const uint8_t *header) {
    uint8_t name_length = header[PIT_OFF_NAME_LENGTH];
    return name_length >= 1U && name_length <= PIT_NAME_FIELD &&
           xx_crc16_xmodem_calc(0U, header, PIT_HEADER_CRC_SPAN) ==
               pit_be16(header + PIT_OFF_HEADER_CRC) &&
           pit_be32(header + PIT_OFF_DATA_LENGTH) <= PIT_MAX_FORK &&
           pit_be32(header + PIT_OFF_RSRC_LENGTH) <= PIT_MAX_FORK;
}

static void pit_member_from_header(pit_member *m, const uint8_t *header) {
    m->name_length = header[PIT_OFF_NAME_LENGTH];
    xx_rt_memcpy(m->name, header + PIT_OFF_NAME, PIT_NAME_FIELD);
    m->type = pit_be32(header + PIT_OFF_TYPE);
    m->finder_flags = pit_be16(header + PIT_OFF_FINDER);
    m->data_length = pit_be32(header + PIT_OFF_DATA_LENGTH);
    m->rsrc_length = pit_be32(header + PIT_OFF_RSRC_LENGTH);
    m->modified = pit_be32(header + PIT_OFF_MODIFIED);
}

/* Reads the member (or end marker) at absolute offset @p at.  A Huffman
 * member is decoded to its end, since only that locates the next member. */
static pit_step pit_parse_member(xx_io_device *device, int64_t at,
                                 int64_t end, pit_reader *r, pit_member *m) {
    uint8_t marker[PIT_MARKER_SIZE];
    uint8_t header[PIT_BODY_HEADER];
    xx_mem_zero(m, sizeof(*m));
    m->offset = at;
    if (end - at < PIT_MARKER_SIZE ||
        !pit_read_at(device, at, marker, sizeof(marker)))
        return PIT_STEP_STOP;
    if (xx_rt_memcmp(marker, "PEnd", 4U) == 0) return PIT_STEP_END;
    if (xx_rt_memcmp(marker, "PMa", 3U) != 0) return PIT_STEP_STOP;
    m->marker = marker[3];
    if (marker[3] == '5' || marker[3] == '6') {
        m->kind = PIT_KIND_ENCRYPTED;
        m->size = end - at;
        return PIT_STEP_ENCRYPTED;
    }
    if (marker[3] == 'g')
        m->kind = PIT_KIND_STORED;
    else if (marker[3] == '4')
        m->kind = PIT_KIND_HUFFMAN;
    else
        return PIT_STEP_STOP;

    pit_reader_start(r, device, NULL, at + PIT_MARKER_SIZE, end,
                     m->kind == PIT_KIND_HUFFMAN);
    if ((r->huffman && !pit_read_tree(r)) ||
        !pit_read_body(r, header, sizeof(header)) ||
        !pit_body_header_valid(header))
        return PIT_STEP_STOP;
    pit_member_from_header(m, header);

    if (m->kind == PIT_KIND_STORED) {
        int64_t size = (int64_t)PIT_STORED_OVERHEAD +
                       (int64_t)m->data_length + (int64_t)m->rsrc_length;
        if (size > end - at) return PIT_STEP_STOP;
        m->size = size;
    } else {
        uint64_t need = (uint64_t)m->data_length +
                        (uint64_t)m->rsrc_length + PIT_TRAILER;
        uint64_t index;
        uint8_t byte;
        if (need > pit_bits_left(r)) return PIT_STEP_STOP;
        for (index = 0U; index < need; ++index)
            if (!pit_symbol(r, &byte)) return PIT_STEP_STOP;
        m->size = PIT_MARKER_SIZE + (int64_t)r->fetched;
    }
    return PIT_STEP_MEMBER;
}

static void pit_stream_free(void *opaque) {
    pit_stream *stream = (pit_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_mem_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->members) xx_mem_free(stream->members);
    xx_mem_free(stream);
}

static bool pit_add_member(pit_stream *stream, const pit_member *member) {
    if (stream->member_count == stream->member_capacity) {
        size_t capacity = stream->member_capacity ? stream->member_capacity * 2U
                                                  : 16U;
        pit_member *grown;
        if (capacity > PIT_MAX_MEMBERS + 1U) capacity = PIT_MAX_MEMBERS + 1U;
        if (capacity <= stream->member_count) return false;
        grown = (pit_member *)xx_mem_realloc(stream->members,
                                             capacity * sizeof(*grown));
        if (!grown) return false;
        stream->members = grown;
        stream->member_capacity = capacity;
    }
    stream->members[stream->member_count++] = *member;
    return true;
}

/* Walks the chain from the base address.  The first member must be a
 * readable stored or Huffman member; after that the walk ends at "PEnd", at
 * an encrypted member (published as a placeholder covering the rest of the
 * input), or at the first thing that is not a member, which is left out as
 * trailing data.  With @p out NULL only the first member is checked. */
static bool pit_parse(Abstractformat *format, pit_stream **out) {
    pit_reader *reader;
    pit_stream *stream = NULL;
    int64_t total, at, end;
    size_t members = 0U;
    bool result = false;

    if (out) *out = NULL;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || format->base_address > total ||
        total - format->base_address < (int64_t)PIT_MARKER_SIZE + 12)
        return false;
    reader = (pit_reader *)xx_mem_calloc(1U, sizeof(*reader));
    if (!reader) return false;
    if (out) {
        stream = (pit_stream *)xx_mem_calloc(1U, sizeof(*stream));
        if (!stream) goto done;
    }
    at = format->base_address;
    end = total;
    for (;;) {
        pit_member member;
        pit_step step;
        if (members >= PIT_MAX_MEMBERS) break;
        step = pit_parse_member(format->device, at, end, reader, &member);
        if (step == PIT_STEP_MEMBER) {
            ++members;
            at += member.size;
            if (!stream) break;
            if (!pit_add_member(stream, &member)) goto done;
            continue;
        }
        if (members == 0U || !stream) goto done;
        if (step == PIT_STEP_END) {
            stream->end_marker = true;
            at += PIT_MARKER_SIZE;
        } else if (step == PIT_STEP_ENCRYPTED) {
            if (!pit_add_member(stream, &member)) goto done;
            stream->encrypted = true;
            at = end;
        }
        break;
    }
    if (members == 0U) goto done;
    if (stream) {
        stream->archive_size = at - format->base_address;
        *out = stream;
        stream = NULL;
    }
    result = true;
done:
    if (stream) pit_stream_free(stream);
    xx_mem_free(reader);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static size_t pit_put_utf8(char *out, uint32_t code) {
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6U));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12U));
    out[1] = (char)(0x80U | ((code >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

/* A PackIt name is one Mac Roman file name, not a path: ':' cannot occur on
 * a Mac and '/' is an ordinary character there.  Everything a file system
 * would read as structure or refuse becomes '_', the rest is converted to
 * UTF-8, and trailing dots and spaces (which Windows drops) are removed, so
 * "." and ".." can never survive. */
static void pit_display_name(const pit_member *m, char *out) {
    size_t index, length = 0U;
    for (index = 0U; index < m->name_length && index < PIT_NAME_FIELD;
         ++index) {
        uint8_t c = m->name[index];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
            c == '|')
            out[length++] = '_';
        else if (c < 0x80U)
            out[length++] = (char)c;
        else
            length += pit_put_utf8(out + length, pit_mac_roman[c - 0x80U]);
    }
    while (length != 0U && (out[length - 1U] == ' ' || out[length - 1U] == '.'))
        --length;
    if (length == 0U) out[length++] = '_';
    out[length] = 0;
}

static char pit_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool pit_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || pit_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* Windows device names, with or without an extension and in any case. */
static bool pit_is_device_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length = xx_str_len(name), stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (pit_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((pit_upper(name[0]) == 'C' && pit_upper(name[1]) == 'O' &&
             pit_upper(name[2]) == 'M') ||
            (pit_upper(name[0]) == 'L' && pit_upper(name[1]) == 'P' &&
             pit_upper(name[2]) == 'T'));
}

/* Output names already handed out, compared without case (the output lands
 * on case-insensitive file systems, which also fold the accented letters
 * Mac Roman has in both cases).  Each slot also keeps the next numeric
 * suffix to try for that name, so repeated duplicates stay linear. */
typedef struct pit_names_s {
    const char **slots;
    uint32_t *hints;
    size_t mask;
} pit_names;

/* Next code point of a name built by pit_display_name (UTF-8 of at most
 * three bytes), upper-cased the way a case-insensitive file system would
 * compare it; 0 at the end.  Folding more than the file system does only
 * costs an extra suffix, folding less could let two records share a file. */
static uint32_t pit_fold_next(const char **cursor) {
    const uint8_t *s = (const uint8_t *)*cursor;
    uint32_t code = s[0];
    size_t used = 1U;
    if (code == 0U) return 0U;
    if ((code & 0xE0U) == 0xC0U && s[1] != 0U) {
        code = ((code & 0x1FU) << 6U) | (s[1] & 0x3FU);
        used = 2U;
    } else if ((code & 0xF0U) == 0xE0U && s[1] != 0U && s[2] != 0U) {
        code = ((code & 0x0FU) << 12U) | ((uint32_t)(s[1] & 0x3FU) << 6U) |
               (s[2] & 0x3FU);
        used = 3U;
    }
    *cursor += used;
    if (code >= 'a' && code <= 'z') return code - 0x20U;
    if (code >= 0xE0U && code <= 0xFEU && code != 0xF7U) return code - 0x20U;
    if (code == 0xFFU) return 0x178U;
    if (code == 0x153U) return 0x152U;
    if (code == 0x131U) return 'I';
    if (code == 0x3C0U) return 0x3A0U;
    return code;
}

static uint32_t pit_hash(const char *name) {
    uint32_t hash = 2166136261U, code;
    while ((code = pit_fold_next(&name)) != 0U) {
        hash ^= code;
        hash *= 16777619U;
    }
    return hash;
}

static bool pit_same_name(const char *left, const char *right) {
    for (;;) {
        uint32_t a = pit_fold_next(&left);
        uint32_t b = pit_fold_next(&right);
        if (a != b) return false;
        if (a == 0U) return true;
    }
}

static bool pit_names_init(pit_names *names, size_t expected) {
    size_t size = 16U;
    xx_mem_zero(names, sizeof(*names));
    while (size < expected * 2U + 2U) size *= 2U;
    names->slots = (const char **)xx_mem_calloc(size, sizeof(*names->slots));
    names->hints = (uint32_t *)xx_mem_calloc(size, sizeof(*names->hints));
    names->mask = size - 1U;
    return names->slots && names->hints;
}

static void pit_names_cleanup(pit_names *names) {
    if (names->slots) xx_mem_free((void *)names->slots);
    if (names->hints) xx_mem_free(names->hints);
    xx_mem_zero(names, sizeof(*names));
}

/* Slot holding @p name, or the empty slot where it would go.  The table is
 * never more than half full, so the probe always ends. */
static size_t pit_names_find(const pit_names *names, const char *name) {
    size_t slot = (size_t)pit_hash(name) & names->mask;
    while (names->slots[slot] && !pit_same_name(names->slots[slot], name))
        slot = (slot + 1U) & names->mask;
    return slot;
}

static bool pit_names_taken(const pit_names *names, const char *name) {
    return names->slots[pit_names_find(names, name)] != NULL;
}

static void pit_names_add(pit_names *names, const char *name) {
    size_t slot = pit_names_find(names, name);
    if (!names->slots[slot]) {
        names->slots[slot] = name;
        names->hints[slot] = 1U;
    }
}

/* "<stem>_<n><extension>", the suffix going before a final extension. */
static void pit_with_suffix(const char *base, uint32_t suffix, char *out) {
    size_t length = xx_str_len(base), stem = length, index;
    char digits[16];
    int count;
    for (index = length; index > 1U; --index) {
        if (base[index - 1U] == '.') {
            stem = index - 1U;
            break;
        }
    }
    count = xx_rt_snprintf(digits, sizeof(digits), "_%u", (unsigned)suffix);
    if (count <= 0) count = 0;
    xx_rt_memcpy(out, base, stem);
    xx_rt_memcpy(out + stem, digits, (size_t)count);
    xx_rt_memcpy(out + stem + (size_t)count, base + stem, length - stem);
    out[length + (size_t)count] = 0;
}

static char *pit_copy(const char *text, const char *suffix) {
    size_t length = xx_str_len(text);
    size_t extra = suffix ? xx_str_len(suffix) : 0U;
    char *copy = (char *)xx_mem_alloc(length + extra + 1U);
    if (!copy) return NULL;
    xx_rt_memcpy(copy, text, length);
    if (extra) xx_rt_memcpy(copy + length, suffix, extra);
    copy[length + extra] = 0;
    return copy;
}

static bool pit_add_item(pit_stream *stream, char *name, size_t member,
                         bool resource) {
    pit_item *item = &stream->items[stream->count++];
    item->name = name;
    item->member = member;
    item->resource = resource;
    return true;
}

/* One or two records per member.  A member's data and resource names come
 * from the same stem, and a stem is only taken when both names it needs are
 * still free, so no record can overwrite another's output. */
static bool pit_build_items(pit_stream *stream) {
    pit_names names;
    size_t index;
    bool result = false;
    xx_mem_zero(&names, sizeof(names));
    if (stream->member_count == 0U ||
        stream->member_count > SIZE_MAX / (2U * sizeof(pit_item)))
        return false;
    stream->items = (pit_item *)xx_mem_calloc(stream->member_count * 2U,
                                              sizeof(pit_item));
    if (!stream->items ||
        !pit_names_init(&names, stream->member_count * 2U))
        goto done;
    for (index = 0U; index < stream->member_count; ++index) {
        const pit_member *m = &stream->members[index];
        char base[PIT_NAME_BUFFER];
        char candidate[PIT_NAME_BUFFER];
        char resource[PIT_NAME_BUFFER];
        bool want_data, want_rsrc;
        size_t base_slot;
        uint32_t suffix = 0U, attempts;
        char *data_name = NULL, *rsrc_name = NULL;

        if (m->kind == PIT_KIND_ENCRYPTED) {
            (void)xx_rt_snprintf(base, sizeof(base), "%s%u",
                                 PIT_ENCRYPTED_NAME, (unsigned)(index + 1U));
            want_data = true;
            want_rsrc = false;
        } else {
            pit_display_name(m, base);
            want_rsrc = m->rsrc_length != 0U;
            want_data = m->data_length != 0U || !want_rsrc;
        }
        base_slot = pit_names_find(&names, base);
        for (attempts = 0U; attempts <= 2U * PIT_MAX_MEMBERS + 2U;
             ++attempts) {
            if (suffix == 0U)
                xx_rt_memcpy(candidate, base, xx_str_len(base) + 1U);
            else
                pit_with_suffix(base, suffix, candidate);
            xx_rt_snprintf(resource, sizeof(resource), "%s.rsrc", candidate);
            if ((!want_data || !pit_names_taken(&names, candidate)) &&
                (!want_rsrc || !pit_names_taken(&names, resource)))
                break;
            if (suffix == 0U && names.slots[base_slot] &&
                names.hints[base_slot] != 0U)
                suffix = names.hints[base_slot];
            else
                ++suffix;
        }
        if (attempts > 2U * PIT_MAX_MEMBERS + 2U) goto done;
        if (suffix != 0U && names.slots[base_slot])
            names.hints[base_slot] = suffix + 1U;
        if (want_data) {
            data_name = pit_copy(candidate, NULL);
            if (!data_name) goto done;
            pit_add_item(stream, data_name, index, false);
            pit_names_add(&names, data_name);
        }
        if (want_rsrc) {
            rsrc_name = pit_copy(resource, NULL);
            if (!rsrc_name) goto done;
            pit_add_item(stream, rsrc_name, index, true);
            pit_names_add(&names, rsrc_name);
        }
    }
    result = true;
done:
    pit_names_cleanup(&names);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

static bool pit_write_all(xx_io_device *sink, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(sink, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Streams one member again from its marker, checks that the body header is
 * still the one the walk saw, passes the wanted fork to @p sink (NULL only
 * verifies) and succeeds only when the fork CRC matches and the member ends
 * exactly where the walk said it does. */
static bool pit_extract(Abstractformat *format, const pit_member *m,
                        bool resource, xx_io_device *sink, xx_pd_struct *pd) {
    pit_reader *r;
    uint8_t header[PIT_BODY_HEADER];
    uint8_t trailer[PIT_TRAILER];
    pit_member check;
    uint16_t crc = 0U;
    uint32_t fork;
    bool result = false;
    if (!format || !m || m->kind == PIT_KIND_ENCRYPTED || m->size <= 0)
        return false;
    r = (pit_reader *)xx_mem_calloc(1U, sizeof(*r));
    if (!r) return false;
    pit_reader_start(r, format->device, NULL, m->offset + PIT_MARKER_SIZE,
                     m->offset + m->size, m->kind == PIT_KIND_HUFFMAN);
    if ((r->huffman && !pit_read_tree(r)) ||
        !pit_read_body(r, header, sizeof(header)) ||
        !pit_body_header_valid(header))
        goto done;
    xx_mem_zero(&check, sizeof(check));
    pit_member_from_header(&check, header);
    if (check.data_length != m->data_length ||
        check.rsrc_length != m->rsrc_length)
        goto done;
    for (fork = 0U; fork < 2U; ++fork) {
        uint64_t left = fork == 0U ? m->data_length : m->rsrc_length;
        bool wanted = sink && (fork == 1U) == resource;
        while (left != 0U) {
            size_t amount = left < PIT_CHUNK ? (size_t)left : PIT_CHUNK;
            if (pd && xx_pd_is_stopped(pd)) goto done;
            if (!pit_read_body(r, r->chunk, amount)) goto done;
            crc = xx_crc16_xmodem_calc(crc, r->chunk, amount);
            if (wanted && !pit_write_all(sink, r->chunk, amount)) goto done;
            left -= amount;
        }
    }
    if (!pit_read_body(r, trailer, sizeof(trailer)) ||
        pit_be16(trailer) != crc ||
        (int64_t)r->fetched != m->size - PIT_MARKER_SIZE)
        goto done;
    result = true;
done:
    xx_mem_free(r);
    return result;
}

bool xx_packit_mac_huffman_decode_memory(const uint8_t *stream,
                                         size_t stream_size, uint8_t *output,
                                         size_t output_size,
                                         size_t *consumed) {
    pit_reader *r;
    size_t index;
    bool result = false;
    if (consumed) *consumed = 0U;
    if (!stream || (!output && output_size != 0U) ||
        stream_size > (size_t)INT64_MAX)
        return false;
    r = (pit_reader *)xx_mem_calloc(1U, sizeof(*r));
    if (!r) return false;
    pit_reader_start(r, NULL, stream, 0, (int64_t)stream_size, true);
    if (!pit_read_tree(r) ||
        (uint64_t)output_size > pit_bits_left(r))
        goto done;
    for (index = 0U; index < output_size; ++index)
        if (!pit_symbol(r, output + index)) goto done;
    if (consumed) *consumed = (size_t)r->fetched;
    result = true;
done:
    xx_mem_free(r);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool pit_copy_options(xx_list_s *destination, const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool pit_set_record(xx_archive_record *record, const pit_stream *stream,
                           const pit_item *item) {
    const pit_member *m = &stream->members[item->member];
    bool encrypted = m->kind == PIT_KIND_ENCRYPTED;
    uint64_t length = item->resource ? m->rsrc_length : m->data_length;
    uint64_t method = (m->kind == PIT_KIND_HUFFMAN || m->marker == '6') ? 1U
                                                                        : 0U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = item->resource ? -1 : m->offset;
    if (m->kind == PIT_KIND_STORED) {
        record->header_size = PIT_MARKER_SIZE + PIT_BODY_HEADER;
        record->data_offset = m->offset + PIT_MARKER_SIZE + PIT_BODY_HEADER +
                              (item->resource ? (int64_t)m->data_length : 0);
        record->compressed_size = (int64_t)length;
    } else {
        /* Both forks share one coded stream (or one ciphertext). */
        record->header_size = PIT_MARKER_SIZE;
        record->data_offset = m->offset + PIT_MARKER_SIZE;
        record->compressed_size = m->size - PIT_MARKER_SIZE;
    }
    if (!xx_archive_record_set_original_name(record, item->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)record->compressed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        method) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         encrypted) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (encrypted) return true;
    return xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          m->modified) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          m->finder_flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS, m->type);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_packit_mac_init(xx_packit_mac *archive, xx_io_device *device,
                        int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_PACKIT_MAC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-packit");
    xx_format_set_extension(&archive->format, "pit");
    archive->format.check_is_valid = xx_packit_mac_check_is_valid;
    archive->format.handle_base_info = xx_packit_mac_handle_base_info;
    archive->format.get_format_size = xx_packit_mac_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_packit_mac_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_packit_mac_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_packit_mac_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_packit_mac_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_packit_mac_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_packit_mac_free_archive_records_reading;
    archive->archive_size = -1;
}

xx_packit_mac *xx_packit_mac_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_packit_mac *archive =
        (xx_packit_mac *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_packit_mac_init(archive, device, base_address);
    return archive;
}

void xx_packit_mac_destroy(xx_packit_mac *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_packit_mac_free(xx_packit_mac *archive) {
    if (!archive) return;
    xx_packit_mac_destroy(archive);
    xx_mem_free(archive);
}

bool xx_packit_mac_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    (void)pd;
    return pit_parse(format, NULL);
}

bool xx_packit_mac_handle_base_info(Abstractformat *format,
                                    xx_pd_struct *pd) {
    pit_stream *stream;
    xx_packit_mac *archive;
    (void)pd;
    if (!format) return false;
    if (!pit_parse(format, &stream) || !pit_build_items(stream)) {
        if (stream) pit_stream_free(stream);
        format->is_valid = false;
        return false;
    }
    archive = (xx_packit_mac *)format;
    archive->number_of_records = stream->count;
    archive->number_of_members = stream->member_count;
    archive->archive_size = stream->archive_size;
    archive->has_end_marker = stream->end_marker;
    archive->has_encrypted = stream->encrypted;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    pit_stream_free(stream);
    return true;
}

int64_t xx_packit_mac_get_format_size(Abstractformat *format,
                                      xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_packit_mac_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_packit_mac_get_number_of_archive_records(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_packit_mac_handle_base_info(format, pd))
               ? ((xx_packit_mac *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_packit_mac_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pit_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!pit_parse(format, &stream)) return NULL;
    if (!pit_build_items(stream) || stream->count == 0U) {
        pit_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pit_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pit_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!pit_copy_options(&state->options, options) ||
        !pit_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_packit_mac_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_packit_mac_archive_record_move_to_next(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    pit_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pit_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = pit_set_record(&state->current_record, stream,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_packit_mac_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    pit_stream *stream;
    const pit_item *item;
    const pit_member *member;
    const xx_var *path_option;
    const xx_var *limit_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pit_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    item = &stream->items[stream->index];
    member = &stream->members[item->member];
    if (member->kind == PIT_KIND_ENCRYPTED) return false;
    limit_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (limit_option &&
        (uint64_t)(item->resource ? member->rsrc_length : member->data_length) >
            xx_var_get_u64(limit_option))
        return false;
    path_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return pit_extract(format, member, item->resource, NULL, pd);
    if (pit_is_device_name(item->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", item->name)
               : xx_str_concat(base, item->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = pit_extract(format, member, item->resource, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_packit_mac_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
