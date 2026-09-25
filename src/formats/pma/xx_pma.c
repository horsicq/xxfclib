/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * PMarc (.pma) archives.
 *
 * PMarc is the CP/M and MSX relative of LHA: it reuses LHA's level-0 member
 * header verbatim and only changes the method tag, so an archive is a chain
 * of members laid end to end from the start of the format, each a header
 * followed by its payload, ending at EOF or at a 0x00 byte where the next
 * header size would be.  CP/M writers then pad the tail of the file to a
 * 128-byte record with 0x1A.
 *
 *   0x00  u8       base header size; counts neither itself nor the checksum
 *                  byte, so the header is that value plus two, minimum 24
 *   0x01  u8       additive checksum of bytes [0x02, size + 2)
 *   0x02  char[5]  method tag, one of "-pm0-", "-pm1-", "-pm2-"
 *   0x07  u32 LE   compressed size
 *   0x0b  u32 LE   uncompressed size
 *   0x0f  u32 LE   MS-DOS time|date
 *   0x13  u8       MS-DOS attribute byte
 *   0x14  u8       header level; PMarc only ever writes 0
 *   0x15  u8       name length n
 *   0x16  char[n]  file name
 *   0x16+n u16 LE  CRC-16/ARC of the uncompressed payload
 *
 * Anything between the name+CRC and the end of the declared header is the
 * archive comment PMarc optionally stores; it is skipped.
 *
 * The three methods:
 *   -pm0-  stored.
 *   -pm1-  LZ77 over a 16 KiB window with static Huffman-ish prefix codes for
 *          the lengths and distances, and byte values encoded as a walk along
 *          a move-to-front history list.  The distance code widens as the
 *          output grows, so early codes need fewer bits.
 *   -pm2-  LZ77 over an 8 KiB window with two built binary trees - one for
 *          the command codes, one for the distance widths - rebuilt at 1, 2,
 *          4 and 8 KiB and every 4 KiB after that.  Byte values again come
 *          from the move-to-front history list.
 *
 * Both bitstreams are ported from the Lhasa implementation by Simon Howard
 * (lib/lha_pm1_decoder.c and lib/lha_pm2_decoder.c, ISC licensed), by way of
 * XArchive's Algos/xlha_legacy_pm1_p.cpp and xlha_legacy_pm2_p.cpp.  The
 * header's CRC-16 is checked against every decode, so a wrong bitstream is
 * reported as a failure rather than handed back as file data.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pma/xx_pma.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#define XX_PMA_MAX_MEMBERS 100000 /* no count is stored: a runaway guard */
#define XX_PMA_MAX_HEADER 257     /* the largest a u8 size byte can describe */
#define XX_PMA_MIN_HEADER 24
#define XX_PMA_MAX_NAME 256
#define XX_PMA_MAX_DECODED (256 * 1024 * 1024)
#define XX_PMA_MAX_TAIL 128 /* one CP/M record of 0x1A padding */

#define XX_PMA_M_PM0 0
#define XX_PMA_M_PM1 1
#define XX_PMA_M_PM2 2

typedef struct xx_pma_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    uint16_t crc16;
    uint8_t attributes;
} xx_pma_member;

typedef struct xx_pma_stream_s {
    xx_pma_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_pma_stream;

static void xx_pma_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_pma_read_at(Abstractformat *self, int64_t offset,
                           uint8_t *buffer, size_t size) {
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received =
            xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) return false;
        completed += (size_t)received;
    }
    return true;
}

static uint16_t xx_pma_le16(const uint8_t *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t xx_pma_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* CRC-16/ARC, the check PMarc stores for the uncompressed payload. */
static uint16_t xx_pma_crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0U;
    size_t index;

    for (index = 0U; index < size; ++index) {
        int bit;
        crc = (uint16_t)(crc ^ data[index]);
        for (bit = 0; bit < 8; ++bit) {
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1U) ? 0xA001U : 0U));
        }
    }
    return crc;
}

/* The additive header checksum: the low byte of the sum of every base-header
 * byte from index 2 on.  With only a four-character tag to key on, this byte
 * is what stops unrelated data from being walked as a member chain, so it is
 * never advisory. */
static bool xx_pma_checksum_ok(const uint8_t *header, int32_t base_size) {
    uint32_t sum = 0U;
    int32_t index;

    if (base_size < 3) return false;
    for (index = 2; index < base_size; ++index) sum += (uint32_t)header[index];
    return (sum & 0xFFU) == (uint32_t)header[1];
}

/* "-pm" + '0'..'2' + "-".  The method character is the whole discriminator
 * between this reader and the general LHA one, so it stays closed: a tag this
 * reader cannot decode must not be walked as if it could be. */
static bool xx_pma_tag_ok(const uint8_t *prefix, uint32_t *method) {
    if (prefix[2] != (uint8_t)'-' || prefix[3] != (uint8_t)'p' ||
        prefix[4] != (uint8_t)'m' || prefix[6] != (uint8_t)'-') {
        return false;
    }
    if (prefix[5] < (uint8_t)'0' || prefix[5] > (uint8_t)'2') return false;
    *method = (uint32_t)(prefix[5] - (uint8_t)'0');
    return true;
}

/* CP/M names are plain ASCII; a control byte where a name should be is the
 * cheapest sign that the walk has wandered into payload. */
static bool xx_pma_name_byte_ok(uint8_t byte) {
    return byte >= 0x20U && byte != 0x7FU;
}

static bool xx_pma_path_safe(const char *name) {
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    if (name[1] == ':') return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 0U) return false;
        if (length == 1U && cursor[0] == '.') return false;
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_pma_stream_free(void *pointer) {
    xx_pma_stream *stream = (xx_pma_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_pma_add(xx_pma_stream *stream, const xx_pma_member *member) {
    xx_pma_member *grown;

    if (stream->count >= (size_t)XX_PMA_MAX_MEMBERS) return false;
    grown = (xx_pma_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* ------------------------------------------------------------ bitstream -- */

/* Bits come out most significant first.  @p pad is the number of zero bytes
 * the reader may serve after the real input runs out: -pm1- streams rely on
 * being able to read a little past their own end, whereas -pm2- streams do
 * not and get none. */
typedef struct xx_pma_bits_s {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t buffer;
    unsigned int bits;
    size_t pad;
} xx_pma_bits;

static void xx_pma_bits_init(xx_pma_bits *reader, const uint8_t *data,
                             size_t size, size_t pad) {
    reader->data = data;
    reader->size = size;
    reader->pos = 0U;
    reader->buffer = 0U;
    reader->bits = 0U;
    reader->pad = pad;
}

static int xx_pma_peek_bits(xx_pma_bits *reader, unsigned int count) {
    if (count == 0U) return 0;
    if (count > 16U) return -1;
    while (reader->bits < count) {
        uint8_t byte;
        if (reader->pos < reader->size) {
            byte = reader->data[reader->pos++];
        } else if (reader->pad != 0U) {
            byte = 0U;
            --reader->pad;
        } else {
            return -1;
        }
        reader->buffer |= (uint32_t)byte << (24U - reader->bits);
        reader->bits += 8U;
    }
    return (int)(reader->buffer >> (32U - count));
}

static int xx_pma_read_bits(xx_pma_bits *reader, unsigned int count) {
    int result = xx_pma_peek_bits(reader, count);

    if (result >= 0) {
        reader->buffer <<= count;
        reader->bits -= count;
    }
    return result;
}

static int xx_pma_read_bit(xx_pma_bits *reader) {
    return xx_pma_read_bits(reader, 1U);
}

/* -------------------------------------------- shared PMarc sub-decoders -- */

typedef struct xx_pma_vlt_s {
    unsigned int offset;
    unsigned int bits;
} xx_pma_vlt;

static int xx_pma_decode_vlt(xx_pma_bits *reader, const xx_pma_vlt *table,
                             unsigned int header) {
    int value = xx_pma_read_bits(reader, table[header].bits);

    if (value < 0) return -1;
    return (int)table[header].offset + value;
}

/* In both bitstreams a literal is not the byte itself but the number of steps
 * to walk back along a move-to-front list of every byte value, so a byte seen
 * recently costs fewer bits.  The initial order groups printable ASCII first,
 * then the control codes, then the rest. */
typedef struct xx_pma_history_s {
    uint8_t prev[256];
    uint8_t next[256];
    uint8_t head;
} xx_pma_history;

static void xx_pma_history_init(xx_pma_history *list) {
    unsigned int index;

    for (index = 0U; index < 256U; ++index) {
        list->prev[index] = (uint8_t)(index + 1U);
        list->next[index] = (uint8_t)(index - 1U);
    }
    list->head = 0x20U;
    list->prev[0x7F] = 0x00U;
    list->next[0x00] = 0x7FU;
    list->prev[0x1F] = 0xA0U;
    list->next[0xA0] = 0x1FU;
    list->prev[0xDF] = 0x80U;
    list->next[0x80] = 0xDFU;
    list->prev[0x9F] = 0xE0U;
    list->next[0xE0] = 0x9FU;
    list->prev[0xFF] = 0x20U;
    list->next[0x20] = 0xFFU;
}

static uint8_t xx_pma_history_find(const xx_pma_history *list, uint8_t count) {
    uint8_t code = list->head;
    unsigned int index;

    /* Walk whichever way round the ring is shorter. */
    if (count < 128U) {
        for (index = 0U; index < (unsigned int)count; ++index) {
            code = list->prev[code];
        }
    } else {
        for (index = 0U; index < 256U - (unsigned int)count; ++index) {
            code = list->next[code];
        }
    }
    return code;
}

static void xx_pma_history_update(xx_pma_history *list, uint8_t byte) {
    uint8_t old_head;
    uint8_t node_prev;
    uint8_t node_next;

    if (list->head == byte) return;
    node_prev = list->prev[byte];
    node_next = list->next[byte];
    list->prev[node_next] = node_prev;
    list->next[node_prev] = node_next;

    old_head = list->head;
    list->prev[byte] = old_head;
    list->next[byte] = list->next[old_head];
    list->prev[list->next[old_head]] = byte;
    list->next[old_head] = byte;
    list->head = byte;
}

/* ----------------------------------------------------- code tree walker -- */

/* A set of codes of differing bit lengths, held as a binary tree inside a
 * byte array: node n has children at tree[n] and tree[n + 1], and the top bit
 * marks a leaf.  Byte elements cap the tree at 127 nodes, which is ample for
 * the two -pm2- trees (65 and 17 entries). */
#define XX_PMA_TREE_LEAF 0x80U

static void xx_pma_tree_init(uint8_t *tree, size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        tree[index] = (uint8_t)XX_PMA_TREE_LEAF;
    }
}

static void xx_pma_tree_single(uint8_t *tree, uint8_t code) {
    tree[0] = (uint8_t)(code | XX_PMA_TREE_LEAF);
}

typedef struct xx_pma_tree_build_s {
    uint8_t *tree;
    size_t length;
    unsigned int allocated;
    unsigned int next_entry;
} xx_pma_tree_builder;

static void xx_pma_tree_expand(xx_pma_tree_builder *build) {
    unsigned int end_offset;
    unsigned int new_nodes = (build->allocated - build->next_entry) * 2U;

    if ((size_t)build->allocated + (size_t)new_nodes > build->length) return;
    end_offset = build->allocated;
    while (build->next_entry < end_offset) {
        build->tree[build->next_entry] = (uint8_t)build->allocated;
        build->allocated += 2U;
        ++build->next_entry;
    }
}

static unsigned int xx_pma_tree_next(xx_pma_tree_builder *build) {
    unsigned int result;

    if (build->next_entry >= build->allocated) return 0U;
    result = build->next_entry;
    ++build->next_entry;
    return result;
}

static bool xx_pma_tree_add_length(xx_pma_tree_builder *build,
                                   const uint8_t *code_lengths,
                                   unsigned int count, unsigned int code_len) {
    bool remaining = false;
    unsigned int index;

    for (index = 0U; index < count; ++index) {
        if (code_lengths[index] == code_len) {
            unsigned int node = xx_pma_tree_next(build);
            build->tree[node] = (uint8_t)(index | XX_PMA_TREE_LEAF);
        } else if (code_lengths[index] > code_len) {
            remaining = true;
        }
    }
    return remaining;
}

static void xx_pma_tree_build(uint8_t *tree, size_t length,
                              const uint8_t *code_lengths,
                              unsigned int count) {
    xx_pma_tree_builder build;
    unsigned int code_len = 0U;

    build.tree = tree;
    build.length = length;
    build.next_entry = 0U;
    build.allocated = 1U;
    do {
        xx_pma_tree_expand(&build);
        ++code_len;
        /* A code length is at most 7 + 127 by construction, so the loop is
         * bounded; the explicit cap only makes that independent of the
         * caller's validation. */
        if (code_len > 255U) break;
    } while (xx_pma_tree_add_length(&build, code_lengths, count, code_len));
}

static int xx_pma_tree_read(xx_pma_bits *reader, const uint8_t *tree) {
    uint8_t code = tree[0];

    while ((code & XX_PMA_TREE_LEAF) == 0U) {
        int bit = xx_pma_read_bit(reader);
        if (bit < 0) return -1;
        code = tree[code + (unsigned int)bit];
    }
    return (int)(code & (uint8_t)~XX_PMA_TREE_LEAF);
}

/* ------------------------------------------------------- -pm2- decoder -- */

#define XX_PMA_PM2_RING 8192
#define XX_PMA_PM2_OUTPUT 256
#define XX_PMA_PM2_CODE_ELEMENTS 65
#define XX_PMA_PM2_OFFSET_ELEMENTS 17

enum {
    XX_PMA_PM2_UNBUILT = 0,
    XX_PMA_PM2_BUILD1,
    XX_PMA_PM2_BUILD2,
    XX_PMA_PM2_BUILD3,
    XX_PMA_PM2_CONTINUING
};

static const xx_pma_vlt xx_pma_pm2_history_decode[] = {
    {0U, 3U},   {8U, 3U},   {16U, 4U},  {32U, 5U},
    {64U, 5U},  {96U, 5U},  {128U, 6U}, {192U, 6U}};

static const xx_pma_vlt xx_pma_pm2_copy_decode[] = {
    {17U, 3U}, {25U, 3U}, {33U, 5U}, {65U, 6U}, {129U, 7U}, {256U, 0U}};

typedef struct xx_pma_pm2_s {
    xx_pma_bits bits;
    int tree_state;
    size_t tree_rebuild_remaining;
    uint8_t ringbuf[XX_PMA_PM2_RING];
    unsigned int ringbuf_pos;
    xx_pma_history history;
    uint8_t code_tree[XX_PMA_PM2_CODE_ELEMENTS];
    int need_offset_tree;
    uint8_t offset_tree[XX_PMA_PM2_OFFSET_ELEMENTS];
} xx_pma_pm2;

static void xx_pma_pm2_read_code_tree(xx_pma_pm2 *decoder) {
    uint8_t code_lengths[31];
    int num_codes;
    int min_code_length;
    int length_bits;
    unsigned int index;

    num_codes = xx_pma_read_bits(&decoder->bits, 5U);
    min_code_length = xx_pma_read_bits(&decoder->bits, 3U);
    if (num_codes < 0 || min_code_length < 0) return;
    /* Codes above 28 would index past the copy table, so the count is capped
     * at 29 entries (0..28). */
    if (num_codes > 29) return;

    decoder->need_offset_tree =
        num_codes >= 10 && !(num_codes == 29 && min_code_length == 0);

    if (min_code_length == 0) {
        if (num_codes < 1) return;
        xx_pma_tree_single(decoder->code_tree, (uint8_t)(num_codes - 1));
        return;
    }
    length_bits = xx_pma_read_bits(&decoder->bits, 3U);
    if (length_bits < 0) return;
    for (index = 0U; index < (unsigned int)num_codes; ++index) {
        int value = xx_pma_read_bits(&decoder->bits,
                                     (unsigned int)length_bits);
        if (value < 0) return;
        code_lengths[index] =
            value == 0 ? 0U : (uint8_t)(min_code_length + value - 1);
    }
    xx_pma_tree_build(decoder->code_tree, sizeof(decoder->code_tree),
                      code_lengths, (unsigned int)num_codes);
}

static void xx_pma_pm2_read_offset_tree(xx_pma_pm2 *decoder,
                                        unsigned int num_offsets) {
    uint8_t offset_lengths[8];
    unsigned int offset;
    unsigned int single_offset = 0U;
    unsigned int num_codes = 0U;

    if (!decoder->need_offset_tree) return;
    if (num_offsets > 8U) return;
    for (offset = 0U; offset < num_offsets; ++offset) {
        int length = xx_pma_read_bits(&decoder->bits, 3U);
        if (length < 0) return;
        offset_lengths[offset] = (uint8_t)length;
        if (length != 0) {
            single_offset = offset;
            ++num_codes;
        }
    }
    if (num_codes == 1U) {
        xx_pma_tree_single(decoder->offset_tree, (uint8_t)single_offset);
        return;
    }
    xx_pma_tree_build(decoder->offset_tree, sizeof(decoder->offset_tree),
                      offset_lengths, num_offsets);
}

static void xx_pma_pm2_rebuild(xx_pma_pm2 *decoder) {
    switch (decoder->tree_state) {
        case XX_PMA_PM2_UNBUILT:
            xx_pma_pm2_read_code_tree(decoder);
            xx_pma_pm2_read_offset_tree(decoder, 5U);
            decoder->tree_state = XX_PMA_PM2_BUILD1;
            decoder->tree_rebuild_remaining = 1024U;
            break;
        case XX_PMA_PM2_BUILD1:
            xx_pma_pm2_read_offset_tree(decoder, 6U);
            decoder->tree_state = XX_PMA_PM2_BUILD2;
            decoder->tree_rebuild_remaining = 1024U;
            break;
        case XX_PMA_PM2_BUILD2:
            xx_pma_pm2_read_offset_tree(decoder, 7U);
            decoder->tree_state = XX_PMA_PM2_BUILD3;
            decoder->tree_rebuild_remaining = 2048U;
            break;
        case XX_PMA_PM2_BUILD3:
            if (xx_pma_read_bit(&decoder->bits) == 1) {
                xx_pma_pm2_read_code_tree(decoder);
            }
            xx_pma_pm2_read_offset_tree(decoder, 8U);
            decoder->tree_state = XX_PMA_PM2_CONTINUING;
            decoder->tree_rebuild_remaining = 4096U;
            break;
        default:
            if (xx_pma_read_bit(&decoder->bits) == 1) {
                xx_pma_pm2_read_code_tree(decoder);
                xx_pma_pm2_read_offset_tree(decoder, 8U);
            }
            decoder->tree_rebuild_remaining = 4096U;
            break;
    }
}

static void xx_pma_pm2_output(xx_pma_pm2 *decoder, uint8_t *buffer,
                              size_t *length, uint8_t byte) {
    decoder->ringbuf[decoder->ringbuf_pos] = byte;
    decoder->ringbuf_pos = (decoder->ringbuf_pos + 1U) % XX_PMA_PM2_RING;
    buffer[*length] = byte;
    ++*length;
    xx_pma_history_update(&decoder->history, byte);
    --decoder->tree_rebuild_remaining;
    if (decoder->tree_rebuild_remaining == 0U) xx_pma_pm2_rebuild(decoder);
}

static void xx_pma_pm2_single_byte(xx_pma_pm2 *decoder, unsigned int code,
                                   uint8_t *buffer, size_t *length) {
    int offset = xx_pma_decode_vlt(&decoder->bits,
                                   xx_pma_pm2_history_decode, code);

    if (offset < 0) return;
    xx_pma_pm2_output(decoder, buffer, length,
                      xx_pma_history_find(&decoder->history,
                                          (uint8_t)offset));
}

static int xx_pma_pm2_copy_count(xx_pma_pm2 *decoder, unsigned int code) {
    if (code < 15U) return (int)code + 2;
    return xx_pma_decode_vlt(&decoder->bits, xx_pma_pm2_copy_decode,
                             code - 15U);
}

static int xx_pma_pm2_copy_offset(xx_pma_pm2 *decoder, unsigned int code) {
    unsigned int bits;
    int result = 0;
    int value;

    if (code == 0U) {
        bits = 6U;
    } else if (code < 20U) {
        value = xx_pma_tree_read(&decoder->bits, decoder->offset_tree);
        if (value < 0) return -1;
        if (value == 0) {
            bits = 6U;
        } else {
            bits = (unsigned int)value + 5U;
            if (bits > 16U) return -1;
            result = 1 << bits;
        }
    } else {
        return 0;
    }
    value = xx_pma_read_bits(&decoder->bits, bits);
    if (value < 0) return -1;
    return result + value;
}

static void xx_pma_pm2_copy(xx_pma_pm2 *decoder, unsigned int code,
                            uint8_t *buffer, size_t *length) {
    int to_copy = xx_pma_pm2_copy_count(decoder, code);
    int offset = xx_pma_pm2_copy_offset(decoder, code);
    unsigned int start;
    unsigned int index;

    if (to_copy < 0 || offset < 0) return;
    if (to_copy > XX_PMA_PM2_OUTPUT) return;
    start = (decoder->ringbuf_pos + XX_PMA_PM2_RING - 1U -
             (unsigned int)offset) %
            XX_PMA_PM2_RING;
    for (index = 0U; index < (unsigned int)to_copy; ++index) {
        unsigned int position = (start + index) % XX_PMA_PM2_RING;
        xx_pma_pm2_output(decoder, buffer, length,
                          decoder->ringbuf[position]);
    }
}

static size_t xx_pma_pm2_step(xx_pma_pm2 *decoder, uint8_t *buffer) {
    size_t result = 0U;
    int code;

    if (decoder->tree_state == XX_PMA_PM2_UNBUILT) {
        /* The very first bit of the stream is discarded. */
        xx_pma_read_bit(&decoder->bits);
        xx_pma_pm2_rebuild(decoder);
    }
    code = xx_pma_tree_read(&decoder->bits, decoder->code_tree);
    if (code < 0) return 0U;
    if (code < 8) {
        xx_pma_pm2_single_byte(decoder, (unsigned int)code, buffer, &result);
    } else {
        xx_pma_pm2_copy(decoder, (unsigned int)code - 8U, buffer, &result);
    }
    return result;
}

static bool xx_pma_pm2_decode(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size) {
    xx_pma_pm2 *decoder;
    uint8_t buffer[XX_PMA_PM2_OUTPUT];
    size_t written = 0U;
    bool result;

    decoder = (xx_pma_pm2 *)xx_mem_alloc(sizeof(*decoder));
    if (!decoder) return false;
    xx_mem_zero(decoder, sizeof(*decoder));
    xx_pma_bits_init(&decoder->bits, input, input_size, 0U);
    decoder->tree_state = XX_PMA_PM2_UNBUILT;
    decoder->tree_rebuild_remaining = 0U;
    xx_rt_memset(decoder->ringbuf, ' ', XX_PMA_PM2_RING);
    decoder->ringbuf_pos = 0U;
    xx_pma_history_init(&decoder->history);
    xx_pma_tree_init(decoder->code_tree, XX_PMA_PM2_CODE_ELEMENTS);
    xx_pma_tree_init(decoder->offset_tree, XX_PMA_PM2_OFFSET_ELEMENTS);

    while (written < output_size) {
        size_t produced = xx_pma_pm2_step(decoder, buffer);
        size_t take;
        if (produced == 0U) break;
        take = produced;
        if (take > output_size - written) take = output_size - written;
        xx_rt_memcpy(output + written, buffer, take);
        written += take;
    }
    result = written == output_size;
    xx_mem_free(decoder);
    return result;
}

/* ------------------------------------------------------- -pm1- decoder -- */

#define XX_PMA_PM1_RING 16384
#define XX_PMA_PM1_MAX_BYTE_BLOCK 216
#define XX_PMA_PM1_MAX_COPY 244
#define XX_PMA_PM1_OUTPUT (XX_PMA_PM1_MAX_BYTE_BLOCK + XX_PMA_PM1_MAX_COPY)
#define XX_PMA_PM1_PAD 16 /* streams legitimately read a little past the end */

/* Entries 0..5 are the distance ranges proper; 6..14 are narrowed stand-ins
 * used early in the stream, while the history is still short enough that the
 * full width would be wasted bits. */
static const xx_pma_vlt xx_pma_pm1_copy_ranges[] = {
    {0U, 6U},    {64U, 8U},   {0U, 6U},    {64U, 9U},    {576U, 11U},
    {2624U, 13U}, {64U, 8U},  {576U, 8U},  {576U, 9U},   {576U, 10U},
    {2624U, 8U}, {2624U, 9U}, {2624U, 10U}, {2624U, 11U}, {2624U, 12U}};

static const xx_pma_vlt xx_pma_pm1_byte_ranges[] = {
    {0U, 4U}, {16U, 4U}, {32U, 5U}, {64U, 6U}, {128U, 6U}, {192U, 6U}};

/* Each row is a miniature binary tree: the first byte is the root and each
 * nybble is either a leaf (0x0a..0x0f, meaning index 0..5 into the byte
 * ranges above) or an offset to the child node.  Which row is in use is
 * chosen by the 5-bit stream header.  Row 17 is malformed in the original
 * PMarc and is reproduced as-is; nothing appears to emit it. */
static const uint8_t xx_pma_pm1_byte_trees[32][5] = {
    {0x12, 0x2d, 0xef, 0x1c, 0xab}, {0x12, 0x23, 0xde, 0xab, 0xcf},
    {0x12, 0x2c, 0xd2, 0xab, 0xef}, {0x12, 0xa2, 0xd2, 0xbc, 0xef},
    {0x12, 0xa2, 0xc2, 0xbd, 0xef}, {0x12, 0xa2, 0xcd, 0xb1, 0xef},
    {0x12, 0xab, 0x12, 0xcd, 0xef}, {0x12, 0xab, 0x1d, 0xc1, 0xef},
    {0x12, 0xab, 0xc1, 0xd1, 0xef}, {0xa1, 0x12, 0x2c, 0xde, 0xbf},
    {0xa1, 0x1d, 0x1c, 0xb1, 0xef}, {0xa1, 0x12, 0x2d, 0xef, 0xbc},
    {0xa1, 0x12, 0xb2, 0xde, 0xcf}, {0xa1, 0x12, 0xbc, 0xd1, 0xef},
    {0xa1, 0x1c, 0xb1, 0xd1, 0xef}, {0xa1, 0xb1, 0x12, 0xcd, 0xef},
    {0xa1, 0xb1, 0xc1, 0xd1, 0xef}, {0x12, 0x1c, 0xde, 0xab, 0x00},
    {0x12, 0xa2, 0xcd, 0xbe, 0x00}, {0x12, 0xab, 0xc1, 0xde, 0x00},
    {0xa1, 0x1d, 0x1c, 0xbe, 0x00}, {0xa1, 0x12, 0xbc, 0xde, 0x00},
    {0xa1, 0x1c, 0xb1, 0xde, 0x00}, {0xa1, 0xb1, 0xc1, 0xde, 0x00},
    {0x1d, 0x1c, 0xab, 0x00, 0x00}, {0x1c, 0xa1, 0xbd, 0x00, 0x00},
    {0x12, 0xab, 0xcd, 0x00, 0x00}, {0xa1, 0x1c, 0xbd, 0x00, 0x00},
    {0xa1, 0xb1, 0xcd, 0x00, 0x00}, {0xa1, 0xbc, 0x00, 0x00, 0x00},
    {0xab, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00}};

typedef struct xx_pma_pm1_s {
    xx_pma_bits bits;
    unsigned int output_pos;
    const uint8_t *byte_tree;
    uint8_t ringbuf[XX_PMA_PM1_RING];
    unsigned int ringbuf_pos;
    xx_pma_history history;
} xx_pma_pm1;

static void xx_pma_pm1_output(xx_pma_pm1 *decoder, uint8_t byte) {
    decoder->ringbuf[decoder->ringbuf_pos] = byte;
    decoder->ringbuf_pos = (decoder->ringbuf_pos + 1U) % XX_PMA_PM1_RING;
    xx_pma_history_update(&decoder->history, byte);
    ++decoder->output_pos;
}

/* Copy lengths use a staircase of ever-wider fields, shortest first. */
static int xx_pma_pm1_copy_count(xx_pma_pm1 *decoder) {
    int value = xx_pma_read_bits(&decoder->bits, 2U);

    if (value < 0) return -1;
    if (value < 3) return value + 3;
    value = xx_pma_read_bits(&decoder->bits, 3U);
    if (value < 0) return -1;
    if (value < 5) return value + 6;
    if (value == 5) {
        value = xx_pma_read_bits(&decoder->bits, 2U);
        return value < 0 ? -1 : value + 11;
    }
    if (value == 6) {
        value = xx_pma_read_bits(&decoder->bits, 3U);
        return value < 0 ? -1 : value + 15;
    }
    value = xx_pma_read_bits(&decoder->bits, 6U);
    if (value < 0) return -1;
    if (value < 62) return value + 23;
    if (value == 62) {
        value = xx_pma_read_bits(&decoder->bits, 5U);
        return value < 0 ? -1 : value + 85;
    }
    value = xx_pma_read_bits(&decoder->bits, 7U);
    return value < 0 ? -1 : value + 117;
}

/* Bits that only start being present once the output is long enough for the
 * choice they encode to exist. */
static int xx_pma_pm1_bit_after(xx_pma_pm1 *decoder, unsigned int threshold,
                                int fallback) {
    if (decoder->output_pos >= threshold) return xx_pma_read_bit(&decoder->bits);
    return fallback;
}

static int xx_pma_pm1_copy_range(xx_pma_pm1 *decoder) {
    int value = xx_pma_read_bit(&decoder->bits);

    if (value < 0) return -1;
    if (value == 0) {
        value = xx_pma_pm1_bit_after(decoder, 576U, 0);
        if (value < 0) return -1;
        if (value != 0) return 4;
        return xx_pma_pm1_bit_after(decoder, 64U, 0);
    }
    value = xx_pma_pm1_bit_after(decoder, 64U, 1);
    if (value < 0) return -1;
    if (value == 0) return 3;
    value = xx_pma_pm1_bit_after(decoder, 2624U, 1);
    if (value < 0) return -1;
    return value != 0 ? 2 : 5;
}

static size_t xx_pma_pm1_copy(xx_pma_pm1 *decoder, uint8_t *buffer) {
    int range_index = xx_pma_pm1_copy_range(decoder);
    int distance;
    int count;
    int index;
    unsigned int copy_index;

    if (range_index < 0) return 0U;
    /* Ranges 0 and 1 are the shorthand for a two-byte copy. */
    if (range_index < 2) {
        count = 2;
    } else {
        count = xx_pma_pm1_copy_count(decoder);
        if (count < 0) return 0U;
    }
    if (range_index == 3) {
        if (decoder->output_pos < 320U) range_index = 6;
    } else if (range_index == 4) {
        if (decoder->output_pos < 832U) range_index = 7;
        else if (decoder->output_pos < 1088U) range_index = 8;
        else if (decoder->output_pos < 1600U) range_index = 9;
    } else if (range_index == 5) {
        if (decoder->output_pos < 2880U) range_index = 10;
        else if (decoder->output_pos < 3136U) range_index = 11;
        else if (decoder->output_pos < 3648U) range_index = 12;
        else if (decoder->output_pos < 4672U) range_index = 13;
        else if (decoder->output_pos < 6720U) range_index = 14;
    }
    distance = xx_pma_decode_vlt(&decoder->bits, xx_pma_pm1_copy_ranges,
                                 (unsigned int)range_index);
    if (distance < 0 || (unsigned int)distance >= decoder->output_pos) {
        return 0U;
    }
    if (count > XX_PMA_PM1_MAX_COPY) return 0U;
    copy_index = (decoder->ringbuf_pos + XX_PMA_PM1_RING -
                  (unsigned int)distance - 1U) %
                 XX_PMA_PM1_RING;
    for (index = 0; index < count; ++index) {
        uint8_t byte = decoder->ringbuf[copy_index];
        buffer[index] = byte;
        xx_pma_pm1_output(decoder, byte);
        copy_index = (copy_index + 1U) % XX_PMA_PM1_RING;
    }
    return (size_t)count;
}

static int xx_pma_pm1_byte_index(xx_pma_pm1 *decoder) {
    const uint8_t *node = decoder->byte_tree;

    if (node[0] == 0U) return 0;
    for (;;) {
        unsigned int child;
        int bit = xx_pma_read_bit(&decoder->bits);
        if (bit < 0) return -1;
        child = bit == 0 ? (unsigned int)((*node >> 4) & 0x0FU)
                         : (unsigned int)(*node & 0x0FU);
        if (child >= 10U) return (int)(child - 10U);
        if (child == 0U) return -1; /* would not advance: malformed row */
        node += child;
        if (node >= decoder->byte_tree + 5) return -1;
    }
}

static int xx_pma_pm1_byte(xx_pma_pm1 *decoder) {
    int index = xx_pma_pm1_byte_index(decoder);
    int count;

    if (index < 0 || index > 5) return -1;
    count = xx_pma_decode_vlt(&decoder->bits, xx_pma_pm1_byte_ranges,
                              (unsigned int)index);
    if (count < 0 || count > 255) return -1;
    return (int)xx_pma_history_find(&decoder->history, (uint8_t)count);
}

static int xx_pma_pm1_block_count(xx_pma_bits *reader) {
    int value = xx_pma_read_bits(reader, 2U);

    if (value < 0) return 0;
    if (value < 3) return value + 1;
    value = xx_pma_read_bits(reader, 3U);
    if (value < 0) return 0;
    if (value < 7) return value + 4;
    value = xx_pma_read_bits(reader, 4U);
    if (value < 0) return 0;
    if (value < 14) return value + 11;
    if (value == 14) {
        value = xx_pma_read_bits(reader, 6U);
        return value < 0 ? 0 : value + 25;
    }
    value = xx_pma_read_bits(reader, 7U);
    return value < 0 ? 0 : value + 89;
}

static size_t xx_pma_pm1_byte_block(xx_pma_pm1 *decoder, uint8_t *buffer) {
    int block_length = xx_pma_pm1_block_count(&decoder->bits);
    size_t result;
    size_t copied;
    int index;

    if (block_length <= 0 || block_length > XX_PMA_PM1_MAX_BYTE_BLOCK) {
        return 0U;
    }
    for (index = 0; index < block_length; ++index) {
        int value = xx_pma_pm1_byte(decoder);
        if (value < 0) return 0U;
        buffer[index] = (uint8_t)value;
        xx_pma_pm1_output(decoder, (uint8_t)value);
    }
    result = (size_t)block_length;
    /* A block always ends because a copy command follows - unless it simply
     * hit the maximum block length. */
    if (result == (size_t)XX_PMA_PM1_MAX_BYTE_BLOCK) return result;
    copied = xx_pma_pm1_copy(decoder, buffer + result);
    if (copied == 0U) return 0U;
    return result + copied;
}

static bool xx_pma_pm1_decode(const uint8_t *input, size_t input_size,
                              uint8_t *output, size_t output_size) {
    xx_pma_pm1 *decoder;
    uint8_t buffer[XX_PMA_PM1_OUTPUT];
    size_t written = 0U;
    int header;
    bool result;

    decoder = (xx_pma_pm1 *)xx_mem_alloc(sizeof(*decoder));
    if (!decoder) return false;
    xx_mem_zero(decoder, sizeof(*decoder));
    xx_pma_bits_init(&decoder->bits, input, input_size, XX_PMA_PM1_PAD);
    xx_pma_history_init(&decoder->history);

    /* The stream opens with a 5-bit index selecting the byte-value tree. */
    header = xx_pma_read_bits(&decoder->bits, 5U);
    if (header < 0) {
        xx_mem_free(decoder);
        return false;
    }
    decoder->byte_tree = xx_pma_pm1_byte_trees[header];

    while (written < output_size) {
        int command = xx_pma_read_bit(&decoder->bits);
        size_t produced;
        size_t take;

        if (command < 0) break;
        produced = command == 0 ? xx_pma_pm1_copy(decoder, buffer)
                                : xx_pma_pm1_byte_block(decoder, buffer);
        if (produced == 0U) break;
        take = produced;
        if (take > output_size - written) take = output_size - written;
        xx_rt_memcpy(output + written, buffer, take);
        written += take;
    }
    result = written == output_size;
    xx_mem_free(decoder);
    return result;
}

/* --------------------------------------------------------------- parse -- */

static xx_pma_stream *xx_pma_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_pma_stream *stream = NULL;
    uint8_t header[XX_PMA_MAX_HEADER];
    char name[XX_PMA_MAX_NAME];
    int64_t total;
    int64_t span;
    int64_t offset = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XX_PMA_MIN_HEADER) return NULL;

    stream = (xx_pma_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    while (offset < span) {
        xx_pma_member member;
        int64_t remaining = span - offset;
        int32_t base_size;
        int32_t name_length;
        int32_t index;
        uint32_t method = 0U;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        size_t out = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (remaining < 22) break; /* only padding can live here */
        if (!xx_pma_read_at(self, self->base_address + offset, header, 22U)) {
            goto fail;
        }
        /* A zero where the header size would be is the end-of-archive
         * marker; CP/M padding follows it. */
        if (header[0] == 0U) break;
        if (!xx_pma_tag_ok(header, &method)) goto fail;
        /* PMarc writes level 0 and nothing else. */
        if (header[20] != 0U) goto fail;

        base_size = (int32_t)header[0] + 2;
        if (base_size < XX_PMA_MIN_HEADER) goto fail;
        if ((int64_t)base_size > remaining) goto fail;
        if (!xx_pma_read_at(self, self->base_address + offset, header,
                            (size_t)base_size)) {
            goto fail;
        }
        if (!xx_pma_checksum_ok(header, base_size)) goto fail;

        name_length = (int32_t)header[21];
        /* The name and the payload CRC behind it must fit inside the header
         * the size byte declared. */
        if (XX_PMA_MIN_HEADER + name_length > base_size) goto fail;
        for (index = 0; index < name_length; ++index) {
            uint8_t byte = header[22 + index];
            if (!xx_pma_name_byte_ok(byte)) goto fail;
            if (byte == (uint8_t)'\\') byte = (uint8_t)'/';
            if (out + 1U >= sizeof(name)) goto fail;
            name[out++] = (char)byte;
        }
        /* CP/M pads names with trailing dots and spaces; they cannot be part
         * of a file name on the extraction side. */
        while (out != 0U && (name[out - 1U] == '.' || name[out - 1U] == ' ')) {
            --out;
        }
        if (out == 0U) goto fail;
        name[out] = '\0';

        compressed_size = xx_pma_le32(header + 7);
        uncompressed_size = xx_pma_le32(header + 11);
        if ((int64_t)compressed_size > remaining - (int64_t)base_size) {
            goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = base_size;
        member.data_offset = self->base_address + offset + (int64_t)base_size;
        member.compressed_size = (int64_t)compressed_size;
        member.uncompressed_size = (int64_t)uncompressed_size;
        member.method = method;
        member.timestamp = (uint64_t)xx_pma_le32(header + 15);
        member.attributes = header[19];
        member.crc16 = xx_pma_le16(header + 22 + name_length);
        if (!xx_pma_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        offset += (int64_t)base_size + (int64_t)compressed_size;
    }

    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    /* What is left has to be the end marker plus CP/M record padding: a
     * single 128-byte record at most, opening with 0x00 or 0x1A.  Anything
     * else means the walk ended somewhere it should not have. */
    if (offset < span) {
        uint8_t tail[XX_PMA_MAX_TAIL];
        int64_t tail_size = span - offset;

        if (tail_size > XX_PMA_MAX_TAIL) goto fail;
        if (!xx_pma_read_at(self, self->base_address + offset, tail,
                            (size_t)tail_size)) {
            goto fail;
        }
        if (tail[0] != 0x00U && tail[0] != 0x1AU) goto fail;
    }

    stream->archive_size = offset;
    return stream;

fail:
    xx_pma_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- decode -- */

static bool xx_pma_decode(Abstractformat *self, const xx_pma_member *member,
                          uint8_t **out, size_t *out_size, xx_pd_struct *pd) {
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t plain_size;
    bool decoded;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    /* The stated size is attacker-controlled, so it is capped before it
     * becomes an allocation. */
    if (member->uncompressed_size > XX_PMA_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;

    if (member->method == XX_PMA_M_PM0 &&
        member->compressed_size != member->uncompressed_size) {
        return false;
    }
    /* Every PMarc bitstream emits at least one code, so a compressed member
     * with no payload, or one that claims no output, is malformed. */
    if (member->method != XX_PMA_M_PM0 &&
        (member->compressed_size == 0 || member->uncompressed_size == 0)) {
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    if (member->compressed_size > 0) {
        packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
        if (!packed) return false;
        if (!xx_pma_read_at(self, member->data_offset, packed,
                            (size_t)member->compressed_size)) {
            xx_mem_free(packed);
            return false;
        }
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    /* xx_mem_alloc(0) returns NULL, which the caller cannot tell from a
     * failure, so an empty member still gets one byte. */
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (member->method == XX_PMA_M_PM0) {
        if (plain_size != 0U) xx_rt_memcpy(plain, packed, plain_size);
        decoded = true;
    } else if (member->method == XX_PMA_M_PM1) {
        decoded = xx_pma_pm1_decode(packed, (size_t)member->compressed_size,
                                    plain, plain_size);
    } else {
        decoded = xx_pma_pm2_decode(packed, (size_t)member->compressed_size,
                                    plain, plain_size);
    }
    xx_mem_free(packed);

    /* The header's CRC-16 is the only thing that separates a correct decode
     * from a plausible one, so it is mandatory rather than advisory. */
    if (!decoded || xx_pma_crc16(plain, plain_size) != member->crc16) {
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = plain_size;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

#ifdef PMA
#define XX_PMA_FILE_TYPE XX_FILE_TYPE_PMA
#else
#define XX_PMA_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

void xx_pma_init(xx_pma *archive, xx_io_device *device,
                 int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_PMA_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzh-compressed");
    xx_format_set_extension(&archive->format, "pma");
    archive->format.check_is_valid = xx_pma_check_is_valid;
    archive->format.handle_base_info = xx_pma_handle_base_info;
    archive->format.get_format_size = xx_pma_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pma_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pma_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pma_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pma_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pma_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pma_free_archive_records_reading;
    archive->format.destroy = xx_pma_vtable_destroy;
}

xx_pma *xx_pma_create(xx_io_device *device, int64_t base_address) {
    xx_pma *archive = (xx_pma *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_pma_init(archive, device, base_address);
    return archive;
}

void xx_pma_destroy(xx_pma *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_pma_free(xx_pma *archive) {
    if (!archive) return;
    xx_pma_destroy(archive);
    xx_mem_free(archive);
}

static void xx_pma_vtable_destroy(Abstractformat *self) {
    xx_pma_destroy((xx_pma *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_pma_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_pma_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_pma_parse(self, pd);
    if (!stream) return false;
    xx_pma_stream_free(stream);
    return true;
}

bool xx_pma_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_pma *archive = (xx_pma *)self;
    xx_pma_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_pma_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_pma_stream_free(stream);
    return true;
}

int64_t xx_pma_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_pma_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_pma *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_pma_set_record(xx_archive_record *record,
                              const xx_pma_member *member) {
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
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->crc16) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_pma_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xx_pma_get_option(const xx_list_s *options,
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

xx_archive_record_state *xx_pma_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_pma_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_pma_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_pma_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_pma_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_pma_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_pma_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_pma_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pma_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_pma_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_pma_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        xx_pma_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pma_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_pma_stream *stream;
    const xx_pma_member *member;
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
    stream = (xx_pma_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_pma_path_safe(member->name)) return false;

    path_option = xx_pma_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * against its CRC without writing anything. */
        result = xx_pma_decode(self, member, &plain, &plain_size, pd);
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
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (!xx_store_create_dirs_a(target_path, false) ||
        !xx_pma_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent =
                xx_io_write(output, plain + completed, plain_size - completed);
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

void xx_pma_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
