/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Amiga LZX archives.
 *
 * Every multi-byte field is little-endian even though the Amiga itself is a
 * big-endian machine: the archiver wrote the fields byte-reversed and every
 * reader since has had to follow.
 *
 *   archive header, 10 bytes, once:
 *     0x00  3 bytes "LZX"
 *     0x03  7 bytes flags and info, not load-bearing for the walk
 *
 *   member header, 31 bytes, repeated:
 *     0x00  u8   Amiga file attributes
 *     0x01  u8   reserved
 *     0x02  u32 LE unpacked size of THIS member
 *     0x06  u32 LE packed size of the stream this member's group ends;
 *                zero for every member that is merged into a later one
 *     0x0a  u8   machine type the member came from
 *     0x0b  u8   pack method: 0 = stored, 2 = LZX
 *     0x0c  u8   flags, bit 0 = merged with the following member
 *     0x0d  u8   reserved
 *     0x0e  u8   comment length, 0..255
 *     0x0f  u8   extract version
 *     0x10  u16  reserved
 *     0x12  u32 LE packed Amiga date/time
 *     0x16  u32 LE CRC-32 of this member's UNPACKED bytes
 *     0x1a  u32 LE CRC-32 of this header, name and comment, computed with
 *                these four bytes read as zero
 *     0x1e  u8   name length, 1..255
 *     0x1f  name, nameLen bytes, then comment, commentLen bytes
 *
 * The packed stream, when there is one, follows the comment. LZX merges: a
 * run of members may share a single compressed stream whose plaintext is
 * their contents concatenated in header order. Each merged member carries a
 * packed size of zero; the run ends at the first member with a nonzero packed
 * size, and that member's packed size, method and data offset describe the
 * whole run. A member's place in the group plaintext is the sum of the
 * unpacked sizes of the members ahead of it in the same group.
 *
 * The member chain has no terminator: it must consume the file exactly, from
 * the end of the archive header to EOF.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/amigalzx/xx_amigalzx.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_AMIGALZX_COPY_CHUNK (64 * 1024)

typedef struct xx_amigalzx_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    /* Where this member's bytes begin inside its group's plaintext, and how
     * long that plaintext is. For a group of one they are 0 and the member's
     * own unpacked size; for a merged group they are what turns one decode of
     * the shared stream into this member's slice. */
    int64_t group_offset;
    int64_t group_plain;
    uint32_t crc32;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
} xx_amigalzx_member;

typedef struct xx_amigalzx_stream_s {
    xx_amigalzx_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    /* One decoded group, kept so that walking a merged group does not decode
     * the same stream once per member. Keyed by the group's data offset,
     * which is unique inside an archive. */
    uint8_t *cache;
    size_t cache_size;
    int64_t cache_key;
} xx_amigalzx_stream;

static void xx_amigalzx_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_amigalzx_read_at(Abstractformat *self, int64_t offset,
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

static bool xx_amigalzx_range_within(int64_t total, int64_t offset,
                                   int64_t size) {
    return offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_amigalzx_path_safe(const char *name) {
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

static void xx_amigalzx_stream_free(void *pointer) {
    xx_amigalzx_stream *stream = (xx_amigalzx_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->cache);
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_amigalzx_add(xx_amigalzx_stream *stream,
                          const xx_amigalzx_member *member) {
    xx_amigalzx_member *grown = (xx_amigalzx_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}


#define XX_AMIGALZX_MAX_MEMBERS 65536
#define XX_AMIGALZX_MAX_GROUP_OUTPUT ((int64_t)1024 * 1024 * 1024)
#define XX_AMIGALZX_ARCHIVE_HEADER_SIZE 10
#define XX_AMIGALZX_ENTRY_HEADER_SIZE 31
#define XX_AMIGALZX_MAX_NAME 255
#define XX_AMIGALZX_METHOD_STORE 0U
#define XX_AMIGALZX_METHOD_LZX 2U
#define XX_AMIGALZX_MAX_DECODED ((int64_t)256 * 1024 * 1024)

/* Forward declarations: the parse and the decode call into each other's
 * helpers. XX_AMIGALZX_MAX_MEMBERS is a runaway guard rather than a format
 * limit, since no member count is stored; XX_AMIGALZX_MAX_GROUP_OUTPUT bounds
 * a running sum of attacker-supplied u32s so a crafted chain cannot overflow
 * it, matching the reference reader's 1 GiB ceiling. */
static uint32_t xx_amigalzx_le32(const uint8_t *data);
static bool xx_amigalzx_name_byte_valid(uint8_t value);
static int64_t xx_amigalzx_normalize_name(uint8_t *name, int64_t length);
static bool xx_amigalzx_close_group(xx_amigalzx_stream *stream, int64_t span, size_t group_start, int64_t group_plain, int64_t data_offset, int64_t packed_size, int64_t base_address, uint32_t method);
static xx_amigalzx_stream *xx_amigalzx_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_amigalzx_decode(Abstractformat *self, const xx_amigalzx_member *member, xx_amigalzx_stream *stream, uint8_t **out, size_t *out_size, xx_pd_struct *pd);


static uint32_t xx_amigalzx_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Amiga file names are Latin-1, so bytes above 0x9F are legitimate accented
 * characters and are kept. What is rejected is the control ranges: C0
 * (below 0x20) and C1 (0x7F..0x9F). This mirrors the reference reader. */
static bool xx_amigalzx_name_byte_valid(uint8_t value) {
    if (value < 0x20U) return false;
    if (value >= 0x7FU && value <= 0x9FU) return false;
    return true;
}

/* Turn the stored Amiga path into a relative POSIX-ish one, in place.
 *
 * Amiga paths separate with '/' but LZX archives written on other hosts use
 * '\\', and a leading volume name followed by ':' ("Work:src/x") is a root
 * designator, not a directory of the archive. Returns the length of the
 * rewritten name, or 0 for a name that must be rejected. */
static int64_t xx_amigalzx_normalize_name(uint8_t *name, int64_t length) {
    int64_t index;
    int64_t start = 0;
    int64_t out = 0;
    int64_t component;

    if (length < 1 || length > XX_AMIGALZX_MAX_NAME) return 0;
    for (index = 0; index < length; ++index) {
        if (!xx_amigalzx_name_byte_valid(name[index])) return 0;
        if (name[index] == '\\') name[index] = '/';
    }
    for (index = 0; index < length; ++index) {
        if (name[index] == ':') {
            /* Everything up to and including the first colon is the volume
             * designator. A second colon is not a path character on the
             * Amiga, so it would mean this is not a real name. */
            start = index + 1;
            break;
        }
    }
    for (index = start; index < length; ++index) {
        if (name[index] == ':') return 0;
    }
    while (start < length && name[start] == '/') ++start;
    for (index = start; index < length; ++index) {
        name[out++] = name[index];
    }
    if (out == 0) return 0;

    /* An empty component means a doubled separator or a trailing one, and
     * "." / ".." would let a member address a path outside the extraction
     * directory. Both are rejections rather than something to clean up. */
    component = 0;
    for (index = 0; index <= out; ++index) {
        if (index == out || name[index] == '/') {
            int64_t begin = index - component;
            if (component == 0) return 0;
            if (component == 1 && name[begin] == '.') return 0;
            if (component == 2 && name[begin] == '.' &&
                name[begin + 1] == '.') {
                return 0;
            }
            component = 0;
        } else {
            ++component;
        }
    }
    return out;
}

/* Close a merged group: every member from group_start onward shares the
 * stream that the member just parsed terminates. Until this runs the group's
 * members hold their substream offset in data_offset and nothing usable in
 * compressed_size, so this is where a group's members become publishable.
 * Returns false if the group does not describe a consistent stream. */
static bool xx_amigalzx_close_group(xx_amigalzx_stream *stream, int64_t span,
                                    size_t group_start, int64_t group_plain,
                                    int64_t data_offset, int64_t packed_size,
                                    int64_t base_address, uint32_t method) {
    size_t index;

    if (method == XX_AMIGALZX_METHOD_STORE) {
        /* A stored group is its members' plaintexts laid end to end, so the
         * packed length must be exactly the group's total unpacked length.
         * This equality is what lets a stored member be addressed on its own;
         * without it the slicing below would run off the stream. */
        if (packed_size != group_plain) return false;
    } else {
        /* The Amiga LZX coder emits whole 16-bit units, so an odd packed
         * length cannot be a real LZX stream. It is cheap, and it is one of
         * the few structural checks this format offers at all. */
        if ((packed_size & 1) != 0) return false;
    }

    for (index = group_start; index < stream->count; ++index) {
        xx_amigalzx_member *member = &stream->items[index];
        int64_t substream = member->data_offset;

        if (substream < 0 || substream > group_plain) return false;
        member->method = method;
        /* Recorded before data_offset is rewritten below: this is the only
         * point at which a member's place in its group is still known. */
        member->group_offset = substream;
        member->group_plain = group_plain;
        if (member->uncompressed_size > group_plain - substream) return false;
        if (method == XX_AMIGALZX_METHOD_STORE) {
            /* Narrow the member to its own bytes inside the stored run. */
            if (!xx_amigalzx_range_within(span, data_offset + substream,
                                          member->uncompressed_size)) {
                return false;
            }
            member->data_offset = base_address + data_offset + substream;
            member->compressed_size = member->uncompressed_size;
        } else {
            /* A merged LZX group has no per-member stream: every member of
             * the group points at the whole thing, and decode refuses it. */
            member->data_offset = base_address + data_offset;
            member->compressed_size = packed_size;
        }
    }
    return true;
}

static xx_amigalzx_stream *xx_amigalzx_parse(Abstractformat *self,
                                             xx_pd_struct *pd) {
    xx_amigalzx_stream *stream;
    xx_amigalzx_member member;
    uint8_t archive_header[XX_AMIGALZX_ARCHIVE_HEADER_SIZE];
    uint8_t header[XX_AMIGALZX_ENTRY_HEADER_SIZE];
    uint8_t name_buffer[XX_AMIGALZX_MAX_NAME + 1];
    int64_t total;
    int64_t span;
    int64_t offset;
    int64_t unpacked_size;
    int64_t packed_size;
    int64_t comment_length;
    int64_t name_length;
    int64_t normalized;
    int64_t variable_size;
    int64_t data_offset;
    int64_t group_plain = 0;
    int64_t index;
    int64_t records = 0;
    size_t group_start = 0U;
    uint32_t method;
    char *name;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* Signature plus at least one member header and a one-byte name. */
    if (span < XX_AMIGALZX_ARCHIVE_HEADER_SIZE +
                   XX_AMIGALZX_ENTRY_HEADER_SIZE + 1) {
        return NULL;
    }
    if (!xx_amigalzx_read_at(self, self->base_address, archive_header,
                             sizeof(archive_header))) {
        return NULL;
    }
    /* Three bytes of magic is weak on its own. What actually keeps this
     * format from claiming unrelated files is the walk below: every member
     * header must validate and the chain must land on EOF to the byte. */
    if (archive_header[0] != 'L' || archive_header[1] != 'Z' ||
        archive_header[2] != 'X') {
        return NULL;
    }

    stream = (xx_amigalzx_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    offset = XX_AMIGALZX_ARCHIVE_HEADER_SIZE;
    while (offset < span) {
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (++records > XX_AMIGALZX_MAX_MEMBERS) goto fail;
        if (!xx_amigalzx_range_within(span, offset,
                                      XX_AMIGALZX_ENTRY_HEADER_SIZE)) {
            goto fail;
        }
        if (!xx_amigalzx_read_at(self, self->base_address + offset, header,
                                 sizeof(header))) {
            goto fail;
        }

        unpacked_size = (int64_t)xx_amigalzx_le32(header + 2);
        packed_size = (int64_t)xx_amigalzx_le32(header + 6);
        method = (uint32_t)header[11];
        comment_length = (int64_t)header[14];
        name_length = (int64_t)header[30];

        /* Only 0 and 2 were ever assigned. Rejecting everything else is the
         * single most selective byte in the member header and the check a
         * later reader will be tempted to widen "just in case". */
        if (method != XX_AMIGALZX_METHOD_STORE &&
            method != XX_AMIGALZX_METHOD_LZX) {
            goto fail;
        }
        /* A zero-length name is not a thing; the field is 1..255. */
        if (name_length < 1) goto fail;
        variable_size = name_length + comment_length;
        if (!xx_amigalzx_range_within(
                span, offset + XX_AMIGALZX_ENTRY_HEADER_SIZE, variable_size)) {
            goto fail;
        }
        if (!xx_amigalzx_read_at(
                self, self->base_address + offset +
                          XX_AMIGALZX_ENTRY_HEADER_SIZE,
                name_buffer, (size_t)name_length)) {
            goto fail;
        }
        normalized = xx_amigalzx_normalize_name(name_buffer, name_length);
        if (normalized <= 0) goto fail;
        name_buffer[normalized] = 0U;

        /* The group plaintext is a running sum of attacker-supplied u32s. */
        if (unpacked_size > XX_AMIGALZX_MAX_GROUP_OUTPUT - group_plain) {
            goto fail;
        }

        data_offset = offset + XX_AMIGALZX_ENTRY_HEADER_SIZE + variable_size;
        if (!xx_amigalzx_range_within(span, data_offset, packed_size)) {
            goto fail;
        }

        name = xx_str_dup((const char *)name_buffer);
        if (!name) goto fail;

        xx_mem_zero(&member, sizeof(member));
        member.name = name;
        member.header_offset = self->base_address + offset;
        member.header_size = XX_AMIGALZX_ENTRY_HEADER_SIZE + variable_size;
        member.uncompressed_size = unpacked_size;
        /* Provisional: this member's offset inside its group's plaintext.
         * close_group turns it into a real data offset once the group's
         * stream is known, and no member is returned before that happens. */
        member.data_offset = group_plain;
        member.compressed_size = 0;
        member.method = method;
        /* CRC-32 of this member's plaintext, standard zlib/PKZIP parameters.
         * It is the only end-to-end check the format offers and decode below
         * refuses a member whose bytes do not reproduce it. */
        member.crc32 = xx_amigalzx_le32(header + 22);
        /* The packed Amiga date/time word is stored verbatim rather than
         * converted: its field layout is not the UNIX epoch and re-deriving
         * it here would invent precision the container does not have. */
        member.timestamp = (uint64_t)xx_amigalzx_le32(header + 18);
        /* LZX stores directories only as the path prefix of a member. */
        member.is_folder = false;
        /* The header CRC-32 at 0x1a (over the header with those four bytes
         * read as zero, then the name and comment) would be a far stronger
         * gate than any field check above. It is deliberately not enforced,
         * matching the reference reader: a wrong reconstruction of its
         * coverage would reject genuine archives, which is worse here than
         * the false positives the exact-EOF walk already catches. */
        if (!xx_amigalzx_add(stream, &member)) {
            xx_str_free(name);
            goto fail;
        }
        group_plain += unpacked_size;

        if (packed_size > 0) {
            if (!xx_amigalzx_close_group(stream, span, group_start,
                                         group_plain, data_offset, packed_size,
                                         self->base_address, method)) {
                goto fail;
            }
            group_start = stream->count;
            group_plain = 0;
        }
        offset = data_offset + packed_size;
    }

    /* Members still pending means the last group was never terminated by a
     * member carrying its packed size: the archive is truncated mid-group and
     * those members address nothing. */
    if (group_start != stream->count) goto fail;
    if (stream->count == 0U) goto fail;
    /* The chain has no end marker, so consuming the file exactly is the only
     * evidence that these really were member headers and not bytes that
     * happened to parse. Loosening this to "<= span" would let arbitrary data
     * behind a "LZX" signature be accepted. */
    if (offset != span) goto fail;
    for (index = 0; index < (int64_t)stream->count; ++index) {
        if (stream->items[index].data_offset < self->base_address) goto fail;
    }
    stream->archive_size = span;
    return stream;

fail:
    xx_amigalzx_stream_free(stream);
    return NULL;
}



/* -------------------------------------------------- Amiga LZX method 2 -- */

/*
 * The LZX bitstream, as unlzx defines it.
 *
 * Input is a sequence of 16-bit big-endian words, and bits leave a word from
 * its low end: the first bit of the stream is bit 0 of the SECOND byte. A
 * multi-bit field is assembled with the first bit consumed as its low bit,
 * which is why the 24-bit block length below reads high byte first and still
 * comes out right.
 *
 * A stream is a chain of blocks:
 *
 *   3 bits   method: 1 = verbatim-with-no-tables (never written), 2, 3
 *   8x3 bits (method 3 only) the offset tree's eight code lengths
 *   24 bits  plaintext bytes this block produces
 *   pretree  20 lengths of 4 bits each, then 256 literal-tree lengths
 *   pretree  20 more lengths, then 512 match-tree lengths
 *
 * Both halves of the 768-entry main tree are coded as a DELTA against the
 * previous block's lengths -- symbol v means (old + 17 - v) % 17 -- so the
 * tables persist across blocks and a block cannot be decoded on its own.
 *
 * The pretrees are frequently INCOMPLETE: their 20 lengths do not satisfy the
 * Kraft equality that a self-contained Huffman table would. That is legal
 * here and rejecting it is the classic way to fail on this format. What must
 * be rejected is the opposite error, an over-subscribed set of lengths.
 *
 * Symbols 0..255 are literals. A symbol at or above 256 packs a length class
 * in bits 5..8 and an offset class in bits 0..4, each naming a base and a
 * count of extra bits; offset class 0 repeats the previous match's offset.
 * In a method-3 block an offset needing three or more extra bits takes its
 * low three bits from the offset tree instead of the bitstream.
 */

#define XX_AMIGALZX_WINDOW 65536
#define XX_AMIGALZX_WINDOW_MASK (XX_AMIGALZX_WINDOW - 1)
#define XX_AMIGALZX_PRETREE_SIZE 20
#define XX_AMIGALZX_OFFSET_SIZE 8
#define XX_AMIGALZX_MAIN_SIZE 768
#define XX_AMIGALZX_MAX_CODE_BITS 16

static const uint8_t xx_amigalzx_extra_bits[32] = {
    0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,  6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14};

static const uint32_t xx_amigalzx_base[32] = {
    0,    1,    2,    3,    4,    6,    8,     12,    16,    24,   32,
    48,   64,   96,   128,  192,  256,  384,   512,   768,   1024, 1536,
    2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 49152};

typedef struct xx_amigalzx_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t control;
    int available;
    bool overrun;
} xx_amigalzx_bits;

/* A canonical prefix code. Codes are handed out in (length, symbol) order,
 * which is what unlzx's table builder does, so decoding canonically finds the
 * same symbol for the same bits. An incomplete code simply leaves patterns
 * unassigned, and a lookup that lands on one fails. */
typedef struct xx_amigalzx_code_s {
    int count[XX_AMIGALZX_MAX_CODE_BITS + 1];
    int first_code[XX_AMIGALZX_MAX_CODE_BITS + 1];
    int first_index[XX_AMIGALZX_MAX_CODE_BITS + 1];
    uint16_t symbols[XX_AMIGALZX_MAIN_SIZE];
    int max_bits;
    bool single;
    uint16_t single_symbol;
} xx_amigalzx_code;

static void xx_amigalzx_bits_init(xx_amigalzx_bits *bits, const uint8_t *data,
                                  size_t size) {
    bits->data = data;
    bits->size = size;
    bits->position = 0U;
    bits->control = 0U;
    bits->available = 0;
    bits->overrun = false;
}

/* Past the end the reader yields zeros and raises overrun; the caller decides
 * whether that mattered by checking it produced the plaintext it promised. */
static uint32_t xx_amigalzx_read_bits(xx_amigalzx_bits *bits, int count) {
    uint32_t result;

    /* Every field in this format is at most sixteen bits, which is what lets
     * one 16-bit refill always be enough and what keeps the shifts below in
     * range. A wider request would be a bug in the caller. */
    if (count <= 0 || count > 16) return 0U;
    while (bits->available < count) {
        uint32_t word = 0U;
        if (bits->position + 2U <= bits->size) {
            word = ((uint32_t)bits->data[bits->position] << 8) |
                   (uint32_t)bits->data[bits->position + 1U];
            bits->position += 2U;
        } else {
            bits->overrun = true;
        }
        bits->control |= word << bits->available;
        bits->available += 16;
    }
    result = bits->control & ((1U << count) - 1U);
    bits->control >>= count;
    bits->available -= count;
    return result;
}

static bool xx_amigalzx_code_build(xx_amigalzx_code *code,
                                   const uint8_t *lengths, int count,
                                   int max_length) {
    int length;
    int index;
    int value = 0;
    int defined = 0;
    int next_index[XX_AMIGALZX_MAX_CODE_BITS + 1];

    xx_mem_zero(code, sizeof(*code));
    if (count <= 0 || count > XX_AMIGALZX_MAIN_SIZE ||
        max_length > XX_AMIGALZX_MAX_CODE_BITS) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (lengths[index] > (uint8_t)max_length) return false;
        if (lengths[index] != 0U) {
            ++code->count[lengths[index]];
            ++defined;
        }
    }
    if (defined == 0) return false;
    if (defined == 1) {
        for (index = 0; index < count; ++index) {
            if (lengths[index] != 0U) {
                code->single = true;
                code->single_symbol = (uint16_t)index;
                return true;
            }
        }
        return false;
    }
    for (length = 1; length <= max_length; ++length) {
        code->first_code[length] = value;
        code->first_index[length] =
            (length == 1) ? 0
                          : code->first_index[length - 1] +
                                code->count[length - 1];
        next_index[length] = code->first_index[length];
        value += code->count[length];
        /* Over-subscription is the one thing that is never legal. An
         * incomplete code, where this ends below the full space, is. */
        if (value > (1 << length)) return false;
        value <<= 1;
        if (code->count[length] != 0) code->max_bits = length;
    }
    for (index = 0; index < count; ++index) {
        uint8_t item = lengths[index];
        if (item != 0U) code->symbols[next_index[item]++] = (uint16_t)index;
    }
    return true;
}

/* Returns the symbol, or -1 for a bit pattern the code does not define. */
static int xx_amigalzx_code_read(xx_amigalzx_bits *bits,
                                 const xx_amigalzx_code *code) {
    int length;
    int value = 0;

    /* A code with a single symbol carries no bits: unlzx's table has that
     * symbol in every slot, so no input is consumed to reach it. */
    if (code->single) return (int)code->single_symbol;
    for (length = 1; length <= code->max_bits; ++length) {
        value = (value << 1) | (int)xx_amigalzx_read_bits(bits, 1);
        if (bits->overrun) return -1;
        if (code->count[length] != 0 &&
            value - code->first_code[length] < code->count[length]) {
            return (int)code->symbols[code->first_index[length] +
                                      (value - code->first_code[length])];
        }
    }
    return -1;
}

/*
 * Read one half of the main tree's code lengths, delta-coded against what
 * they were in the previous block and themselves coded with a 20-symbol
 * pretree given as twenty 4-bit lengths.
 *
 * The two halves do not use the same run lengths: the 512-entry match half
 * shifts every run down by one and gives the long run an extra bit. That
 * asymmetry is real, it is in unlzx, and getting it wrong desynchronises the
 * bitstream one block in and produces tables that look almost plausible.
 */
static bool xx_amigalzx_read_lengths(xx_amigalzx_bits *bits, uint8_t *lengths,
                                     int count, bool second_half) {
    uint8_t pretree_lengths[XX_AMIGALZX_PRETREE_SIZE];
    xx_amigalzx_code pretree;
    const int fix = second_half ? 1 : 0;
    int index;

    for (index = 0; index < XX_AMIGALZX_PRETREE_SIZE; ++index) {
        pretree_lengths[index] = (uint8_t)xx_amigalzx_read_bits(bits, 4);
    }
    if (bits->overrun) return false;
    if (!xx_amigalzx_code_build(&pretree, pretree_lengths,
                                XX_AMIGALZX_PRETREE_SIZE, 15)) {
        return false;
    }

    index = 0;
    while (index < count) {
        int symbol = xx_amigalzx_code_read(bits, &pretree);
        int repeat = 1;
        int length = 0;

        if (symbol < 0) return false;
        if (symbol <= 16) {
            length = (lengths[index] + 17 - symbol) % 17;
        } else if (symbol == 17) {
            repeat = (int)xx_amigalzx_read_bits(bits, 4) + 4 - fix;
        } else if (symbol == 18) {
            repeat = (int)xx_amigalzx_read_bits(bits, 5 + fix) + 20 - fix;
        } else if (symbol == 19) {
            int again;
            repeat = (int)xx_amigalzx_read_bits(bits, 1) + 4 - fix;
            again = xx_amigalzx_code_read(bits, &pretree);
            if (again < 0 || again > 16) return false;
            length = (lengths[index] + 17 - again) % 17;
        } else {
            return false;
        }
        if (bits->overrun) return false;
        if (repeat <= 0 || repeat > count - index) return false;
        while (repeat-- > 0) lengths[index++] = (uint8_t)length;
    }
    return true;
}

/* Decode a whole LZX stream into @p output, which must be exactly the
 * plaintext length the container declared for the group. */
static bool xx_amigalzx_lzx_decode(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   xx_pd_struct *pd) {
    xx_amigalzx_bits bits;
    xx_amigalzx_code main_code;
    xx_amigalzx_code offset_code;
    uint8_t *lengths;
    size_t produced = 0U;
    size_t block_end = 0U;
    uint32_t last_offset = 1U;
    int method = 0;
    bool result = false;

    if (!input || !output || output_size == 0U) return false;
    /* Every unit the coder emits is a 16-bit word. */
    if ((input_size & 1U) != 0U || input_size < 2U) return false;

    /* 768 bytes, but it must survive across blocks and the rest of this
     * function is already deep in stack. */
    lengths = (uint8_t *)xx_mem_alloc(XX_AMIGALZX_MAIN_SIZE);
    if (!lengths) return false;
    xx_mem_zero(lengths, XX_AMIGALZX_MAIN_SIZE);
    xx_mem_zero(&offset_code, sizeof(offset_code));
    xx_mem_zero(&main_code, sizeof(main_code));
    xx_amigalzx_bits_init(&bits, input, input_size);

    while (produced < output_size) {
        int symbol;

        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (produced >= block_end) {
            uint32_t high;
            uint32_t middle;
            uint32_t low;
            size_t block_size;

            method = (int)xx_amigalzx_read_bits(&bits, 3);
            if (method < 2 || method > 3) goto done;
            if (method == 3) {
                uint8_t offset_lengths[XX_AMIGALZX_OFFSET_SIZE];
                int index;
                for (index = 0; index < XX_AMIGALZX_OFFSET_SIZE; ++index) {
                    offset_lengths[index] =
                        (uint8_t)xx_amigalzx_read_bits(&bits, 3);
                }
                if (!xx_amigalzx_code_build(&offset_code, offset_lengths,
                                            XX_AMIGALZX_OFFSET_SIZE, 7)) {
                    goto done;
                }
            }
            high = xx_amigalzx_read_bits(&bits, 8);
            middle = xx_amigalzx_read_bits(&bits, 8);
            low = xx_amigalzx_read_bits(&bits, 8);
            if (bits.overrun) goto done;
            block_size = ((size_t)high << 16) | ((size_t)middle << 8) |
                         (size_t)low;
            /* A block that claims more than the group has left cannot be
             * real, and this is what keeps block_end inside the buffer. */
            if (block_size == 0U || block_size > output_size - produced) {
                goto done;
            }
            block_end = produced + block_size;

            if (!xx_amigalzx_read_lengths(&bits, lengths, 256, false) ||
                !xx_amigalzx_read_lengths(&bits, lengths + 256, 512, true) ||
                !xx_amigalzx_code_build(&main_code, lengths,
                                        XX_AMIGALZX_MAIN_SIZE,
                                        XX_AMIGALZX_MAX_CODE_BITS)) {
                goto done;
            }
        }

        symbol = xx_amigalzx_code_read(&bits, &main_code);
        if (symbol < 0 || symbol >= XX_AMIGALZX_MAIN_SIZE) goto done;
        if (symbol < 256) {
            output[produced++] = (uint8_t)symbol;
            continue;
        }
        {
            const int offset_class = symbol & 31;
            const int offset_bits = (int)xx_amigalzx_extra_bits[offset_class];
            const int length_class = ((symbol - 256) >> 5) & 15;
            uint32_t offset = xx_amigalzx_base[offset_class];
            uint32_t length;
            uint32_t index;

            if (offset == 0U) {
                offset = last_offset;
            } else if (method == 3 && offset_bits >= 3) {
                int low_bits;
                offset += xx_amigalzx_read_bits(&bits, offset_bits - 3) << 3;
                low_bits = xx_amigalzx_code_read(&bits, &offset_code);
                if (low_bits < 0 || low_bits > 7) goto done;
                offset += (uint32_t)low_bits;
            } else {
                offset += xx_amigalzx_read_bits(&bits, offset_bits);
            }
            length = xx_amigalzx_base[length_class] + 3U +
                     xx_amigalzx_read_bits(
                         &bits, (int)xx_amigalzx_extra_bits[length_class]);
            if (bits.overrun) goto done;
            if (offset == 0U || offset > (uint32_t)XX_AMIGALZX_WINDOW) {
                goto done;
            }
            if ((size_t)length > block_end - produced) goto done;
            for (index = 0U; index < length; ++index) {
                /* The coder's window starts zero-filled, so a reference that
                 * reaches back before anything was produced reads zeros
                 * rather than running off the buffer. */
                output[produced] = (offset > produced)
                                       ? 0U
                                       : output[produced - offset];
                ++produced;
            }
            last_offset = offset;
        }
    }
    result = produced == output_size;

done:
    xx_mem_free(lengths);
    return result;
}

/* ------------------------------------------------------------ extract --- */

/* Decode the group @p member belongs to, caching the result on the stream so
 * that a merged group is decoded once rather than once per member. */
static const uint8_t *xx_amigalzx_group_plain(Abstractformat *self,
                                              xx_amigalzx_stream *stream,
                                              const xx_amigalzx_member *member,
                                              xx_pd_struct *pd) {
    uint8_t *packed;
    uint8_t *plain;

    if (!stream || member->group_plain <= 0) return NULL;
    if (stream->cache && stream->cache_key == member->data_offset &&
        stream->cache_size == (size_t)member->group_plain) {
        return stream->cache;
    }
    if (member->compressed_size <= 0 ||
        member->compressed_size > XX_AMIGALZX_MAX_DECODED ||
        member->group_plain > XX_AMIGALZX_MAX_DECODED) {
        return NULL;
    }

    packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
    if (!packed) return NULL;
    if (!xx_amigalzx_read_at(self, member->data_offset, packed,
                             (size_t)member->compressed_size)) {
        xx_mem_free(packed);
        return NULL;
    }
    plain = (uint8_t *)xx_mem_alloc((size_t)member->group_plain);
    if (!plain) {
        xx_mem_free(packed);
        return NULL;
    }
    if (!xx_amigalzx_lzx_decode(packed, (size_t)member->compressed_size, plain,
                                (size_t)member->group_plain, pd)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return NULL;
    }
    xx_mem_free(packed);

    xx_mem_free(stream->cache);
    stream->cache = plain;
    stream->cache_size = (size_t)member->group_plain;
    stream->cache_key = member->data_offset;
    return stream->cache;
}

/* Extraction of an Amiga LZX member.
 *
 * Method 0 is per-member even inside a merged group: the "packed" bytes of a
 * stored group are just its members' plaintexts laid end to end, so parse has
 * already narrowed each member to its own slice.
 *
 * Method 2 decodes the whole group -- a merged group's stream produces the
 * concatenation of its members, and there is no way to start in the middle of
 * it -- and then hands back this member's slice of that plaintext.
 *
 * Either way the result is checked against the member's stored CRC-32 before
 * it is returned. That checksum is the format's only end-to-end statement
 * about the plaintext, and without it a subtly wrong decode is indistinguish-
 * able from a right one. */
static bool xx_amigalzx_decode(Abstractformat *self,
                               const xx_amigalzx_member *member,
                               xx_amigalzx_stream *stream, uint8_t **out,
                               size_t *out_size, xx_pd_struct *pd) {
    uint8_t *buffer;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->compressed_size > XX_AMIGALZX_MAX_DECODED ||
        member->uncompressed_size > XX_AMIGALZX_MAX_DECODED) {
        return false;
    }
    if (member->method != XX_AMIGALZX_METHOD_STORE &&
        member->method != XX_AMIGALZX_METHOD_LZX) {
        return false;
    }

    if (member->uncompressed_size == 0) {
        /* A real, empty member. xx_mem_alloc(0) is not worth relying on. */
        buffer = (uint8_t *)xx_mem_alloc(1U);
        if (!buffer) return false;
        if (member->crc32 != 0U) {
            xx_mem_free(buffer);
            return false;
        }
        *out = buffer;
        *out_size = 0U;
        return true;
    }

    buffer = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!buffer) return false;

    if (member->method == XX_AMIGALZX_METHOD_STORE) {
        /* parse narrows a stored member to exactly its own bytes, so the two
         * sizes must agree; a disagreement means the narrowing was skipped
         * and the copy below would hand out a neighbour's data. */
        if (member->compressed_size != member->uncompressed_size ||
            !xx_amigalzx_read_at(self, member->data_offset, buffer,
                                 (size_t)member->uncompressed_size)) {
            xx_mem_free(buffer);
            return false;
        }
    } else {
        const uint8_t *plain =
            xx_amigalzx_group_plain(self, stream, member, pd);
        if (!plain || member->group_offset < 0 ||
            member->group_offset > member->group_plain ||
            member->uncompressed_size >
                member->group_plain - member->group_offset) {
            xx_mem_free(buffer);
            return false;
        }
        xx_rt_memcpy(buffer, plain + member->group_offset,
                     (size_t)member->uncompressed_size);
    }

    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(buffer);
        return false;
    }
    if (xx_crc32_calc(0U, buffer, (size_t)member->uncompressed_size) !=
        member->crc32) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = (size_t)member->uncompressed_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_amigalzx_init(xx_amigalzx *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_AMIGALZX;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-amiga-lzx");
    xx_format_set_extension(&archive->format, "lzx");
    archive->format.check_is_valid = xx_amigalzx_check_is_valid;
    archive->format.handle_base_info = xx_amigalzx_handle_base_info;
    archive->format.get_format_size = xx_amigalzx_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_amigalzx_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_amigalzx_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_amigalzx_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_amigalzx_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_amigalzx_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_amigalzx_free_archive_records_reading;
    archive->format.destroy = xx_amigalzx_vtable_destroy;
}

xx_amigalzx *xx_amigalzx_create(xx_io_device *device, int64_t base_address) {
    xx_amigalzx *archive = (xx_amigalzx *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_amigalzx_init(archive, device, base_address);
    return archive;
}

void xx_amigalzx_destroy(xx_amigalzx *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_amigalzx_free(xx_amigalzx *archive) {
    if (!archive) return;
    xx_amigalzx_destroy(archive);
    xx_mem_free(archive);
}

static void xx_amigalzx_vtable_destroy(Abstractformat *self) {
    xx_amigalzx_destroy((xx_amigalzx *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_amigalzx_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_amigalzx_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_amigalzx_parse(self, pd);
    if (!stream) return false;
    xx_amigalzx_stream_free(stream);
    return true;
}

bool xx_amigalzx_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_amigalzx *archive = (xx_amigalzx *)self;
    xx_amigalzx_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_amigalzx_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_amigalzx_stream_free(stream);
    return true;
}

int64_t xx_amigalzx_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_amigalzx_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_amigalzx *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_amigalzx_set_record(xx_archive_record *record,
                                 const xx_amigalzx_member *member) {
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

static bool xx_amigalzx_copy_options(xx_list_s *target,
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

static const xx_var *xx_amigalzx_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_amigalzx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_amigalzx_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_amigalzx_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_amigalzx_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_amigalzx_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_amigalzx_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_amigalzx_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_amigalzx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_amigalzx_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_amigalzx_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_amigalzx_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_amigalzx_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

bool xx_amigalzx_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_amigalzx_stream *stream;
    const xx_amigalzx_member *member;
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
    stream = (xx_amigalzx_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_amigalzx_path_safe(member->name)) return false;

    path_option = xx_amigalzx_get_option(&state->options,
                                       XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_amigalzx_decode(self, member, stream, &plain, &plain_size, pd);
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
        !xx_amigalzx_decode(self, member, stream, &plain, &plain_size, pd)) {
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

void xx_amigalzx_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
