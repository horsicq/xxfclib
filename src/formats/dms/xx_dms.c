/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * The seven decompressors below are a C99 port of the DMS decoder from the
 * "ancient" library, Copyright (C) Teemu Suutari; the local reference copy
 * is XArchive/Algos/xancientdmsdecoder_p.cpp together with the Huffman, VLC
 * and bit-reader helpers it pulls in.  Only what DMS needs was taken.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/dms/xx_dms.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is resolved through the alias macro that
 * the enumerator will define. Once XX_FILE_TYPE_DMS lands the alias is
 * defined and this picks it up with no further change. */
#ifdef DMS
#define XX_DMS_FILE_TYPE XX_FILE_TYPE_DMS
#else
#define XX_DMS_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- on-disk sizes ------------------------------------------------------ */

#define XX_DMS_HEADER_SIZE 56U
#define XX_DMS_TRACK_HEADER_SIZE 20U
#define XX_DMS_TRACK_SIZE_DD 11264U
#define XX_DMS_TRACK_SIZE_HD 22528U
#define XX_DMS_TRACKS_PER_DISK 80U

/* Compression modes. */
#define XX_DMS_MODE_NONE 0U
#define XX_DMS_MODE_SIMPLE 1U
#define XX_DMS_MODE_QUICK 2U
#define XX_DMS_MODE_MEDIUM 3U
#define XX_DMS_MODE_DEEP 4U
#define XX_DMS_MODE_HEAVY1 5U
#define XX_DMS_MODE_HEAVY2 6U
#define XX_DMS_MODE_MAX 6U

/* Bombing defence. DMS describes a floppy, so the decoded image has a hard
 * natural ceiling of 80 HD tracks; a header claiming more is refused at
 * PARSE time, never merely bounded at extraction. The packed ceiling is
 * generous - an incompressible disk plus per-track headers - and is checked
 * against the device as well. */
#define XX_DMS_MAX_IMAGE_SIZE (XX_DMS_TRACK_SIZE_HD * XX_DMS_TRACKS_PER_DISK)
#define XX_DMS_MAX_PACKED_SIZE INT64_C(0x800000) /* 8 MiB */
/* One chunk per track plus the informational ones real encoders emit. */
#define XX_DMS_MAX_TRACKS 256U
/* The largest LZ window any mode uses, and the largest intermediate buffer
 * a track header can ask for (the field is a u16). */
#define XX_DMS_MAX_CONTEXT_SIZE 16384U
#define XX_DMS_MAX_TMP_SIZE 65536U

/* Huffman node budgets. The symbol table is read with a 9-bit count and a
 * 5-bit length, so at most 511 codes of at most 31 bits; the offset table
 * with a 5-bit count and a 4-bit length, so at most 31 codes of at most 15.
 * Each code contributes at most length + 1 nodes. */
#define XX_DMS_SYMBOL_NODES 16400U
#define XX_DMS_OFFSET_NODES 560U
/* The DEEP mode's adaptive Huffman alphabet: 256 literals plus 58 lengths. */
#define XX_DMS_DEEP_SYMBOLS 314U
#define XX_DMS_DEEP_NODES (XX_DMS_DEEP_SYMBOLS * 2U - 1U)
#define XX_DMS_DEEP_ROOT (XX_DMS_DEEP_SYMBOLS * 2U - 2U)

static const uint16_t xx_dms_crc16_table[256] = {
    0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241, 0xc601,
    0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440, 0xcc01, 0x0cc0,
    0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40, 0x0a00, 0xcac1, 0xcb81,
    0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841, 0xd801, 0x18c0, 0x1980, 0xd941,
    0x1b00, 0xdbc1, 0xda81, 0x1a40, 0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01,
    0x1dc0, 0x1c80, 0xdc41, 0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0,
    0x1680, 0xd641, 0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081,
    0x1040, 0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
    0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441, 0x3c00,
    0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41, 0xfa01, 0x3ac0,
    0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840, 0x2800, 0xe8c1, 0xe981,
    0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41, 0xee01, 0x2ec0, 0x2f80, 0xef41,
    0x2d00, 0xedc1, 0xec81, 0x2c40, 0xe401, 0x24c0, 0x2580, 0xe541, 0x2700,
    0xe7c1, 0xe681, 0x2640, 0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0,
    0x2080, 0xe041, 0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281,
    0x6240, 0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
    0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41, 0xaa01,
    0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840, 0x7800, 0xb8c1,
    0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41, 0xbe01, 0x7ec0, 0x7f80,
    0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40, 0xb401, 0x74c0, 0x7580, 0xb541,
    0x7700, 0xb7c1, 0xb681, 0x7640, 0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101,
    0x71c0, 0x7080, 0xb041, 0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0,
    0x5280, 0x9241, 0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481,
    0x5440, 0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
    0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841, 0x8801,
    0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40, 0x4e00, 0x8ec1,
    0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41, 0x4400, 0x84c1, 0x8581,
    0x4540, 0x8701, 0x47c0, 0x4680, 0x8641, 0x8201, 0x42c0, 0x4380, 0x8341,
    0x4100, 0x81c1, 0x8081, 0x4040};

static uint16_t xx_dms_crc16(const uint8_t *data, size_t size) {
    uint16_t accumulator = 0U;
    size_t index;
    if (!data) return 0U;
    for (index = 0U; index < size; ++index) {
        accumulator = (uint16_t)((accumulator >> 8) ^
                                 xx_dms_crc16_table[(accumulator & 0xffU) ^
                                                    data[index]]);
    }
    return accumulator;
}

/* The per-track checksum is a plain additive sum truncated to 16 bits. */
static uint16_t xx_dms_checksum(const uint8_t *data, size_t size) {
    uint16_t total = 0U;
    size_t index;
    if (!data) return 0U;
    for (index = 0U; index < size; ++index) {
        total = (uint16_t)(total + data[index]);
    }
    return total;
}

/* --- one track as the parser recorded it -------------------------------- */

typedef struct xx_dms_track_s {
    uint32_t number;       /**< Raw track number from the header. */
    int64_t header_offset; /**< Absolute device offset of the "TR" header. */
    int64_t data_offset;   /**< Absolute device offset of the packed chunk. */
    uint32_t packed_size;
    uint32_t tmp_size;
    uint32_t raw_size;
    uint32_t image_offset; /**< Offset of this track inside the raw image. */
    uint8_t flags;
    uint8_t mode;
    bool is_info; /**< Track 80 or >= 0x8000: metadata, not disk content. */
} xx_dms_track;

typedef struct xx_dms_private_s {
    xx_dms_track *tracks;
    size_t count;      /**< Tracks recorded, informational ones included. */
    size_t data_count; /**< Tracks that contribute image bytes. */
    int64_t input_size;
    int64_t archive_end;
    uint32_t packed_size;
    uint32_t raw_size;
    uint32_t raw_offset;
    uint32_t image_size;
    uint32_t track_size;
    uint32_t context_size; /**< Largest LZ window any recorded track needs. */
    uint32_t tmp_size;     /**< Largest intermediate buffer any track needs. */
    bool is_hd;
    bool is_obfuscated;
} xx_dms_private;

/* --- bit level plumbing -------------------------------------------------- */

/* A forward byte stream over the in-memory copy of the packed archive. The
 * reference throws on overrun; here the failure is latched and the caller's
 * loop notices it, which reproduces the reference's "catch and keep what was
 * produced" behaviour without unwinding. */
typedef struct xx_dms_input_s {
    const uint8_t *data;
    size_t offset;
    size_t end;
    bool failed;
} xx_dms_input;

static uint8_t xx_dms_read_byte(xx_dms_input *input) {
    if (!input || input->offset >= input->end) {
        if (input) input->failed = true;
        return 0U;
    }
    return input->data[input->offset++];
}

typedef struct xx_dms_bits_s {
    xx_dms_input *input;
    uint32_t content;
    uint8_t length;
} xx_dms_bits;

static void xx_dms_bits_reset(xx_dms_bits *bits) {
    if (!bits) return;
    bits->content = 0U;
    bits->length = 0U;
}

/* MSB-first, byte fed. count is never more than 32 in this decoder. */
static uint32_t xx_dms_read_bits(xx_dms_bits *bits, uint32_t count) {
    uint32_t result = 0U;
    if (!bits || count > 32U) {
        if (bits && bits->input) bits->input->failed = true;
        return 0U;
    }
    while (count != 0U) {
        uint8_t take;
        if (bits->length == 0U) {
            bits->content = xx_dms_read_byte(bits->input);
            bits->length = 8U;
        }
        take = (count < (uint32_t)bits->length) ? (uint8_t)count : bits->length;
        bits->length = (uint8_t)(bits->length - take);
        result = (result << take) |
                 ((bits->content >> bits->length) &
                  ((take == 32U) ? 0xffffffffU : ((1U << take) - 1U)));
        count -= take;
    }
    return result;
}

static uint32_t xx_dms_read_bit(xx_dms_bits *bits) {
    return xx_dms_read_bits(bits, 1U);
}

/* A bounded output window over the assembled image. */
typedef struct xx_dms_output_s {
    uint8_t *data;
    size_t offset;
    size_t end;
    bool failed;
} xx_dms_output;

static void xx_dms_write_byte(xx_dms_output *output, uint8_t value) {
    if (!output || output->offset >= output->end) {
        if (output) output->failed = true;
        return;
    }
    output->data[output->offset++] = value;
}

static bool xx_dms_output_eof(const xx_dms_output *output) {
    return !output || output->offset >= output->end;
}

/* --- static Huffman, as used by the two heavy modes ---------------------- */

typedef struct xx_dms_huff_node_s {
    uint32_t left;
    uint32_t right;
    uint32_t value;
} xx_dms_huff_node;

typedef struct xx_dms_huff_s {
    xx_dms_huff_node *nodes;
    size_t count;
    size_t capacity;
    uint32_t empty_value; /**< Used when the table holds no codes at all. */
} xx_dms_huff;

static void xx_dms_huff_reset(xx_dms_huff *huff) {
    if (!huff) return;
    huff->count = 0U;
    huff->empty_value = 0U;
}

/* Insert one code, exactly as the reference builds its tree: walk from the
 * most significant bit down, creating the spine as it goes. */
static bool xx_dms_huff_insert(xx_dms_huff *huff, uint32_t length,
                               uint32_t code, uint32_t value) {
    size_t index = 0U;
    int32_t current_bit;
    if (!huff || !huff->nodes || length > 32U) return false;
    for (current_bit = (int32_t)length; current_bit >= 0; --current_bit) {
        uint32_t code_bit =
            (current_bit != 0 &&
             ((code >> (uint32_t)(current_bit - 1)) & 1U) != 0U)
                ? 1U
                : 0U;
        if (index != huff->count) {
            uint32_t *branch;
            if (current_bit == 0 ||
                (huff->nodes[index].left == 0U &&
                 huff->nodes[index].right == 0U)) {
                return false;
            }
            branch = code_bit ? &huff->nodes[index].right
                              : &huff->nodes[index].left;
            if (*branch == 0U) {
                *branch = (uint32_t)huff->count;
                index = huff->count;
            } else {
                index = *branch;
            }
        } else {
            if (huff->count >= huff->capacity) return false;
            huff->nodes[huff->count].left =
                (current_bit != 0 && code_bit == 0U)
                    ? (uint32_t)(huff->count + 1U)
                    : 0U;
            huff->nodes[huff->count].right =
                (current_bit != 0 && code_bit != 0U)
                    ? (uint32_t)(huff->count + 1U)
                    : 0U;
            huff->nodes[huff->count].value = (current_bit != 0) ? 0U : value;
            ++huff->count;
            index = huff->count;
        }
    }
    return true;
}

/* Build the canonical ("orderly") table Deflate and friends also use. */
static bool xx_dms_huff_create_orderly(xx_dms_huff *huff,
                                       const uint8_t *lengths,
                                       uint32_t table_length) {
    uint16_t first_index[33];
    uint16_t last_index[33];
    uint16_t next_index[512];
    uint32_t min_depth = 32U;
    uint32_t max_depth = 0U;
    uint32_t depth;
    uint32_t index;
    uint32_t code = 0U;
    if (!huff || !lengths || table_length == 0U || table_length > 512U) {
        return false;
    }
    for (index = 0U; index < 33U; ++index) {
        first_index[index] = 0xffffU;
        last_index[index] = 0U;
    }
    for (index = 0U; index < table_length; ++index) next_index[index] = 0U;
    for (index = 0U; index < table_length; ++index) {
        uint32_t length = lengths[index];
        if (length > 32U) return false;
        if (length == 0U) continue;
        if (length < min_depth) min_depth = length;
        if (length > max_depth) max_depth = length;
        if (first_index[length] == 0xffffU) {
            first_index[length] = (uint16_t)index;
            last_index[length] = (uint16_t)index;
        } else {
            next_index[last_index[length]] = (uint16_t)index;
            last_index[length] = (uint16_t)index;
        }
    }
    if (max_depth == 0U) return false;
    for (depth = min_depth; depth <= max_depth; ++depth) {
        if (first_index[depth] != 0xffffU) {
            next_index[last_index[depth]] = (uint16_t)table_length;
        }
        for (index = first_index[depth]; index < table_length;
             index = next_index[index]) {
            if (!xx_dms_huff_insert(huff, depth, code >> (max_depth - depth),
                                    index)) {
                return false;
            }
            code += 1U << (max_depth - depth);
        }
    }
    return true;
}

static uint32_t xx_dms_huff_decode(const xx_dms_huff *huff,
                                   xx_dms_bits *bits) {
    size_t index = 0U;
    if (!huff) {
        if (bits && bits->input) bits->input->failed = true;
        return 0U;
    }
    /* An empty table is legal: the encoder emitted a single repeated value
     * and stored it in place of the code lengths. */
    if (huff->count == 0U) return huff->empty_value;
    while (huff->nodes[index].left != 0U || huff->nodes[index].right != 0U) {
        index = xx_dms_read_bit(bits) ? huff->nodes[index].right
                                      : huff->nodes[index].left;
        if (index == 0U || index >= huff->count) {
            if (bits && bits->input) bits->input->failed = true;
            return 0U;
        }
        if (bits && bits->input && bits->input->failed) return 0U;
    }
    return huff->nodes[index].value;
}

/* --- adaptive Huffman, as used by the DEEP mode -------------------------- */

typedef struct xx_dms_deep_node_s {
    uint32_t frequency;
    uint32_t index;
    uint32_t parent;
    uint32_t left_leaf;
    uint32_t right_leaf;
} xx_dms_deep_node;

typedef struct xx_dms_deep_s {
    xx_dms_deep_node nodes[XX_DMS_DEEP_NODES];
    uint32_t code_map[XX_DMS_DEEP_NODES];
    bool ready;
} xx_dms_deep;

static void xx_dms_deep_reset(xx_dms_deep *deep) {
    uint32_t index;
    uint32_t inner;
    uint32_t leaf;
    if (!deep) return;
    for (index = 0U; index < XX_DMS_DEEP_SYMBOLS; ++index) {
        deep->nodes[index].frequency = 1U;
        deep->nodes[index].index = index;
        deep->nodes[index].parent = XX_DMS_DEEP_SYMBOLS + (index >> 1U);
        deep->nodes[index].left_leaf = 0U;
        deep->nodes[index].right_leaf = 0U;
        deep->code_map[index] = index;
    }
    for (inner = XX_DMS_DEEP_SYMBOLS, leaf = 0U; inner < XX_DMS_DEEP_NODES;
         ++inner, leaf += 2U) {
        deep->nodes[inner].frequency = deep->nodes[leaf].frequency +
                                       deep->nodes[leaf + 1U].frequency;
        deep->nodes[inner].index = inner;
        deep->nodes[inner].parent = XX_DMS_DEEP_SYMBOLS + (inner >> 1U);
        deep->nodes[inner].left_leaf = leaf;
        deep->nodes[inner].right_leaf = leaf + 1U;
        deep->code_map[inner] = inner;
    }
    deep->ready = true;
}

static uint32_t xx_dms_deep_decode(const xx_dms_deep *deep,
                                   xx_dms_bits *bits) {
    uint32_t code = XX_DMS_DEEP_ROOT;
    uint32_t guard = 0U;
    if (!deep || !deep->ready) {
        if (bits && bits->input) bits->input->failed = true;
        return 0U;
    }
    while (code >= XX_DMS_DEEP_SYMBOLS) {
        code = xx_dms_read_bit(bits) ? deep->nodes[code].right_leaf
                                     : deep->nodes[code].left_leaf;
        /* The tree is built here, never read from the file, so these two
         * guards can only fire on a logic error - but an out-of-range index
         * would be an out-of-bounds read, so they stay. */
        if (code >= XX_DMS_DEEP_NODES || ++guard > XX_DMS_DEEP_NODES) {
            if (bits && bits->input) bits->input->failed = true;
            return 0U;
        }
        if (bits && bits->input && bits->input->failed) return 0U;
    }
    return code;
}

static uint32_t *xx_dms_deep_parent_leaf(xx_dms_deep *deep, uint32_t code) {
    xx_dms_deep_node *parent = &deep->nodes[deep->nodes[code].parent];
    return (parent->left_leaf == code) ? &parent->left_leaf
                                       : &parent->right_leaf;
}

static void xx_dms_deep_swap(uint32_t *left, uint32_t *right) {
    uint32_t temporary = *left;
    *left = *right;
    *right = temporary;
}

static void xx_dms_deep_update(xx_dms_deep *deep, uint32_t code) {
    if (!deep || code >= XX_DMS_DEEP_SYMBOLS) return;
    while (code != XX_DMS_DEEP_ROOT) {
        uint32_t index;
        uint32_t dest_index;
        uint32_t frequency;
        ++deep->nodes[code].frequency;
        index = deep->nodes[code].index;
        dest_index = index;
        frequency = deep->nodes[code].frequency;
        while (dest_index != XX_DMS_DEEP_ROOT &&
               frequency >
                   deep->nodes[deep->code_map[dest_index + 1U]].frequency) {
            ++dest_index;
        }
        if (index != dest_index) {
            uint32_t dest_code = deep->code_map[dest_index];
            xx_dms_deep_swap(&deep->nodes[code].index,
                             &deep->nodes[dest_code].index);
            xx_dms_deep_swap(&deep->code_map[index],
                             &deep->code_map[dest_index]);
            /* The parent links must be swapped before the parents are, so
             * that each lookup still resolves against the old parent. */
            xx_dms_deep_swap(xx_dms_deep_parent_leaf(deep, code),
                             xx_dms_deep_parent_leaf(deep, dest_code));
            xx_dms_deep_swap(&deep->nodes[code].parent,
                             &deep->nodes[dest_code].parent);
        }
        code = deep->nodes[code].parent;
        if (code >= XX_DMS_DEEP_NODES) return;
    }
    ++deep->nodes[code].frequency;
}

/* Halve every frequency, rounding up, and rebuild the tree around the new
 * ordering. Called when the root frequency saturates. */
static void xx_dms_deep_halve(xx_dms_deep *deep) {
    uint32_t index;
    uint32_t slot;
    uint32_t inner;
    uint32_t leaf;
    if (!deep) return;
    for (index = 0U, slot = 0U;
         index < XX_DMS_DEEP_NODES - 1U && slot < XX_DMS_DEEP_SYMBOLS;
         ++index) {
        if (deep->code_map[index] < XX_DMS_DEEP_SYMBOLS) {
            deep->nodes[deep->code_map[index]].index = slot++;
        }
    }
    for (index = 0U; index < XX_DMS_DEEP_SYMBOLS; ++index) {
        deep->nodes[index].frequency = (deep->nodes[index].frequency + 1U) >> 1U;
        deep->nodes[index].parent =
            XX_DMS_DEEP_SYMBOLS + (deep->nodes[index].index >> 1U);
        deep->code_map[deep->nodes[index].index] = index;
    }
    for (inner = XX_DMS_DEEP_SYMBOLS, leaf = 0U;
         inner < XX_DMS_DEEP_NODES; ++inner, leaf += 2U) {
        uint32_t left = deep->code_map[leaf];
        uint32_t right = deep->code_map[leaf + 1U];
        uint32_t frequency;
        uint32_t position;
        if (left >= XX_DMS_DEEP_NODES || right >= XX_DMS_DEEP_NODES) return;
        frequency = deep->nodes[left].frequency + deep->nodes[right].frequency;
        deep->nodes[inner].frequency = frequency;
        deep->nodes[inner].index = inner;
        deep->nodes[inner].parent = XX_DMS_DEEP_SYMBOLS + (inner >> 1U);
        deep->nodes[inner].left_leaf = left;
        deep->nodes[inner].right_leaf = right;
        deep->code_map[inner] = inner;
        /* Bubble the new internal node down to its place. The "position > 0"
         * guard is not in the reference; the invariant makes it unreachable,
         * but without it a broken state would index code_map[-1]. */
        for (position = inner;
             position > 0U &&
             frequency < deep->nodes[deep->code_map[position - 1U]].frequency;
             --position) {
            uint32_t code = deep->code_map[position];
            uint32_t dest_code = deep->code_map[position - 1U];
            xx_dms_deep_swap(&deep->nodes[code].index,
                             &deep->nodes[dest_code].index);
            xx_dms_deep_swap(&deep->nodes[code].parent,
                             &deep->nodes[dest_code].parent);
            xx_dms_deep_swap(&deep->code_map[position],
                             &deep->code_map[position - 1U]);
        }
    }
}

/* --- the variable length code table shared by MEDIUM and DEEP ------------ */

#define XX_DMS_VLC_COUNT 16U

typedef struct xx_dms_vlc_s {
    uint8_t bit_lengths[XX_DMS_VLC_COUNT];
    uint32_t offsets[XX_DMS_VLC_COUNT];
} xx_dms_vlc;

static void xx_dms_vlc_init(xx_dms_vlc *vlc) {
    static const uint8_t lengths[XX_DMS_VLC_COUNT] = {7, 7, 8,  8,  8,  9,
                                                      9, 9, 9,  10, 10, 10,
                                                      11, 11, 11, 12};
    uint32_t total = 0U;
    uint32_t index;
    if (!vlc) return;
    for (index = 0U; index < XX_DMS_VLC_COUNT; ++index) {
        vlc->bit_lengths[index] = lengths[index];
        vlc->offsets[index] = total;
        total += 1U << lengths[index];
    }
}

/* --- decoder state ------------------------------------------------------- */

typedef struct xx_dms_decoder_s {
    xx_dms_input input;
    xx_dms_bits bits;
    xx_dms_output output;
    uint8_t *raw;      /**< The assembled image. */
    size_t raw_size;
    uint8_t *context;  /**< The LZ window, shared across tracks. */
    uint32_t context_size;
    uint32_t context_location;
    uint8_t *tmp;      /**< Intermediate buffer for the two stage modes. */
    uint32_t tmp_size;
    bool init_context;
    xx_dms_deep deep;
    xx_dms_huff symbol_decoder;
    xx_dms_huff offset_decoder;
    bool symbol_ready;
    bool offset_ready;
    bool heavy_last_initialized;
    uint32_t heavy_last_offset;
    xx_dms_vlc vlc;
} xx_dms_decoder;

static uint32_t xx_dms_vlc_decode(const xx_dms_vlc *vlc, xx_dms_bits *bits,
                                  uint32_t base) {
    if (!vlc || base >= XX_DMS_VLC_COUNT) {
        if (bits && bits->input) bits->input->failed = true;
        return 0U;
    }
    return vlc->offsets[base] + xx_dms_read_bits(bits, vlc->bit_lengths[base]);
}

/* MEDIUM's distance escape: the top four bits come from the length symbol
 * that was just decoded, the rest from the stream. */
static uint32_t xx_dms_vlc_decode_distance(const xx_dms_vlc *vlc,
                                           xx_dms_bits *bits, uint32_t base,
                                           uint32_t count) {
    uint32_t bit_count;
    if (!vlc || base >= XX_DMS_VLC_COUNT) {
        if (bits && bits->input) bits->input->failed = true;
        return 0U;
    }
    bit_count = vlc->bit_lengths[base];
    if (bit_count < 4U) {
        if (bits && bits->input) bits->input->failed = true;
        return 0U;
    }
    return vlc->offsets[base] + (((count & 0xfU) << (bit_count - 4U)) |
                                 xx_dms_read_bits(bits, bit_count - 4U));
}

static void xx_dms_init_context(xx_dms_decoder *decoder) {
    if (!decoder || !decoder->init_context) return;
    if (decoder->context && decoder->context_size != 0U) {
        xx_mem_zero(decoder->context, decoder->context_size);
    }
    decoder->context_location = 0U;
    decoder->deep.ready = false;
    decoder->init_context = false;
}

static bool xx_dms_stalled(const xx_dms_decoder *decoder,
                           const xx_dms_output *output) {
    return decoder->input.failed || output->failed;
}

/* --- the seven modes ----------------------------------------------------- */

/* Mode 0: the chunk is the track. */
static void xx_dms_unpack_none(xx_dms_decoder *decoder,
                               xx_dms_output *output) {
    while (!xx_dms_output_eof(output) && !decoder->input.failed) {
        xx_dms_write_byte(output, xx_dms_read_byte(&decoder->input));
        if (output->failed) break;
    }
}

/* The RLE stage: 0x90 introduces a run, 0x90 0x00 is a literal 0x90, and a
 * count of 0xff escapes to a 16-bit count. Mode 1 runs it on the chunk
 * directly; modes 2..4 and the heavy modes with flag bit 2 run it over the
 * output of their LZ stage. */
static void xx_dms_unrle(xx_dms_output *output, xx_dms_input *input) {
    while (!xx_dms_output_eof(output) && !input->failed) {
        uint8_t value = xx_dms_read_byte(input);
        uint32_t count = 1U;
        uint32_t step;
        if (value == 0x90U) {
            count = xx_dms_read_byte(input);
            if (count == 0U) {
                count = 1U;
            } else {
                value = xx_dms_read_byte(input);
            }
            if (count == 0xffU) {
                count = (uint32_t)xx_dms_read_byte(input) << 8U;
                count |= xx_dms_read_byte(input);
            }
        }
        if (input->failed) break;
        for (step = 0U; step < count; ++step) {
            xx_dms_write_byte(output, value);
            if (output->failed) break;
        }
        if (output->failed) break;
    }
}

/* Mode 2: a 256-byte window, two-bit lengths, eight-bit distances. */
static void xx_dms_unpack_quick(xx_dms_decoder *decoder,
                                xx_dms_output *output) {
    xx_dms_init_context(decoder);
    while (!xx_dms_output_eof(output) && !xx_dms_stalled(decoder, output)) {
        if (xx_dms_read_bits(&decoder->bits, 1U)) {
            uint8_t value = (uint8_t)xx_dms_read_bits(&decoder->bits, 8U);
            decoder->context[decoder->context_location++] = value;
            xx_dms_write_byte(output, value);
            decoder->context_location &= 0xffU;
        } else {
            uint32_t count = xx_dms_read_bits(&decoder->bits, 2U) + 2U;
            uint8_t offset = (uint8_t)(decoder->context_location -
                                       xx_dms_read_bits(&decoder->bits, 8U) -
                                       1U);
            uint32_t step;
            for (step = 0U; step < count; ++step) {
                uint8_t value = decoder->context[(step + offset) & 0xffU];
                decoder->context[decoder->context_location++] = value;
                xx_dms_write_byte(output, value);
                decoder->context_location &= 0xffU;
                if (output->failed) break;
            }
        }
    }
    if (!xx_dms_stalled(decoder, output)) {
        decoder->context_location = (decoder->context_location + 5U) & 0xffU;
    }
}

/* Mode 3: a 16 KiB window with the shared variable length code table used
 * for both the length and, four bits at a time, the distance. */
static void xx_dms_unpack_medium(xx_dms_decoder *decoder,
                                 xx_dms_output *output) {
    xx_dms_init_context(decoder);
    while (!xx_dms_output_eof(output) && !xx_dms_stalled(decoder, output)) {
        if (xx_dms_read_bits(&decoder->bits, 1U)) {
            uint8_t value = (uint8_t)xx_dms_read_bits(&decoder->bits, 8U);
            decoder->context[decoder->context_location++] = value;
            xx_dms_write_byte(output, value);
            decoder->context_location &= 0x3fffU;
        } else {
            uint32_t count = xx_dms_vlc_decode(
                &decoder->vlc, &decoder->bits,
                xx_dms_read_bits(&decoder->bits, 4U));
            uint32_t raw_distance = xx_dms_vlc_decode_distance(
                &decoder->vlc, &decoder->bits, (count >> 4U) & 0xfU, count);
            uint32_t offset;
            uint32_t step;
            count = (count >> 8U) + 3U;
            offset = decoder->context_location - raw_distance - 1U;
            for (step = 0U; step < count; ++step) {
                uint8_t value = decoder->context[(step + offset) & 0x3fffU];
                decoder->context[decoder->context_location++] = value;
                xx_dms_write_byte(output, value);
                decoder->context_location &= 0x3fffU;
                if (output->failed) break;
            }
        }
    }
    if (!xx_dms_stalled(decoder, output)) {
        decoder->context_location = (decoder->context_location + 66U) & 0x3fffU;
    }
}

/* Mode 4: the same 16 KiB window, but the literal/length alphabet is an
 * adaptive Huffman tree carried across tracks. */
static void xx_dms_unpack_deep(xx_dms_decoder *decoder,
                               xx_dms_output *output) {
    xx_dms_init_context(decoder);
    if (!decoder->deep.ready) xx_dms_deep_reset(&decoder->deep);
    while (!xx_dms_output_eof(output) && !xx_dms_stalled(decoder, output)) {
        uint32_t symbol = xx_dms_deep_decode(&decoder->deep, &decoder->bits);
        if (decoder->input.failed) break;
        if (decoder->deep.nodes[XX_DMS_DEEP_ROOT].frequency == 0x8000U) {
            xx_dms_deep_halve(&decoder->deep);
        }
        xx_dms_deep_update(&decoder->deep, symbol);
        if (symbol < 256U) {
            decoder->context[decoder->context_location++] = (uint8_t)symbol;
            xx_dms_write_byte(output, (uint8_t)symbol);
            decoder->context_location &= 0x3fffU;
        } else {
            uint32_t count = symbol - 253U; /* the shortest match is three */
            uint32_t offset =
                decoder->context_location -
                xx_dms_vlc_decode(&decoder->vlc, &decoder->bits,
                                  xx_dms_read_bits(&decoder->bits, 4U)) -
                1U;
            uint32_t step;
            for (step = 0U; step < count; ++step) {
                uint8_t value = decoder->context[(step + offset) & 0x3fffU];
                decoder->context[decoder->context_location++] = value;
                xx_dms_write_byte(output, value);
                decoder->context_location &= 0x3fffU;
                if (output->failed) break;
            }
        }
    }
    if (!xx_dms_stalled(decoder, output)) {
        decoder->context_location = (decoder->context_location + 60U) & 0x3fffU;
    }
}

/* Read one of the heavy modes' two Huffman tables. A zero code count means
 * the alphabet collapsed to a single value, which follows in place of the
 * code lengths. */
static void xx_dms_read_table(xx_dms_decoder *decoder, xx_dms_huff *huff,
                              bool *ready, uint32_t count_bits,
                              uint32_t value_bits) {
    uint8_t lengths[512];
    uint32_t count;
    uint32_t index;
    uint64_t kraft_sum = 0U;
    xx_dms_huff_reset(huff);
    *ready = false;
    count = xx_dms_read_bits(&decoder->bits, count_bits);
    if (count == 0U) {
        huff->empty_value = xx_dms_read_bits(&decoder->bits, count_bits);
        *ready = !decoder->input.failed;
        return;
    }
    if (count > sizeof(lengths)) {
        decoder->input.failed = true;
        return;
    }
    for (index = 0U; index < count; ++index) {
        uint32_t bits = xx_dms_read_bits(&decoder->bits, value_bits);
        if (bits != 0U) {
            /* Refuse an over-subscribed code set before the slow table
             * build sees it, exactly as the reference does. */
            kraft_sum += UINT64_C(1) << (32U - bits);
            if (kraft_sum > (UINT64_C(1) << 32U)) {
                decoder->input.failed = true;
                return;
            }
        }
        lengths[index] = (uint8_t)bits;
    }
    if (decoder->input.failed) return;
    if (!xx_dms_huff_create_orderly(huff, lengths, count)) {
        decoder->input.failed = true;
        return;
    }
    *ready = true;
}

/* Modes 5 and 6: a 4 KiB or 8 KiB window with two static Huffman tables,
 * and a "same distance as last time" escape. */
static void xx_dms_unpack_heavy(xx_dms_decoder *decoder,
                                xx_dms_output *output, bool init_tables,
                                bool use_8k) {
    uint32_t mask = use_8k ? 0x1fffU : 0xfffU;
    uint32_t bit_length = use_8k ? 14U : 13U;
    xx_dms_init_context(decoder);
    /* The reference notes this reset is outside initContext and cannot
     * explain why; it is reproduced as-is because the bitstreams depend on
     * it. */
    if (!decoder->heavy_last_initialized) {
        decoder->heavy_last_offset = use_8k ? 0U : 0xffffffffU;
        decoder->heavy_last_initialized = true;
    }
    if (init_tables) {
        xx_dms_read_table(decoder, &decoder->symbol_decoder,
                          &decoder->symbol_ready, 9U, 5U);
        xx_dms_read_table(decoder, &decoder->offset_decoder,
                          &decoder->offset_ready, 5U, 4U);
    }
    /* A track that uses the tables without ever having read them is
     * malformed. The reference dereferences the null decoder here; this
     * refuses the track instead. */
    if (!decoder->symbol_ready || !decoder->offset_ready) {
        decoder->input.failed = true;
        return;
    }
    while (!xx_dms_output_eof(output) && !xx_dms_stalled(decoder, output)) {
        uint32_t symbol =
            xx_dms_huff_decode(&decoder->symbol_decoder, &decoder->bits);
        if (decoder->input.failed) break;
        if (symbol < 256U) {
            decoder->context[decoder->context_location++] = (uint8_t)symbol;
            xx_dms_write_byte(output, (uint8_t)symbol);
            decoder->context_location &= mask;
        } else {
            uint32_t count = symbol - 253U;
            uint32_t offset_length =
                xx_dms_huff_decode(&decoder->offset_decoder, &decoder->bits);
            uint32_t raw_offset = decoder->heavy_last_offset;
            uint32_t offset;
            uint32_t step;
            if (decoder->input.failed) break;
            if (offset_length != bit_length) {
                if (offset_length != 0U) {
                    if (offset_length > 32U) {
                        decoder->input.failed = true;
                        break;
                    }
                    raw_offset = (1U << (offset_length - 1U)) |
                                 xx_dms_read_bits(&decoder->bits,
                                                  offset_length - 1U);
                } else {
                    raw_offset = 0U;
                }
                decoder->heavy_last_offset = raw_offset;
            }
            offset = decoder->context_location - raw_offset - 1U;
            for (step = 0U; step < count; ++step) {
                uint8_t value = decoder->context[(step + offset) & mask];
                decoder->context[decoder->context_location++] = value;
                xx_dms_write_byte(output, value);
                decoder->context_location &= mask;
                if (output->failed) break;
            }
        }
    }
}

/* --- track driver -------------------------------------------------------- */

/* A track that stopped short of a 1 KiB sector boundary did not decode; the
 * reference treats that as fatal for the archive and so does this. */
static bool xx_dms_handle_track_size(const xx_dms_output *output) {
    return xx_dms_output_eof(output) || (output->offset & 0x3ffU) == 0U;
}

/* The heavy modes are allowed to lose the very last byte of a track, which
 * the stored additive checksum lets us reconstruct. */
static bool xx_dms_apply_fix(xx_dms_decoder *decoder, xx_dms_output *output,
                             uint32_t mode, uint32_t raw_length,
                             size_t image_offset, uint16_t file_sum) {
    size_t missing;
    uint16_t proto_sum;
    if (mode < XX_DMS_MODE_HEAVY1) return xx_dms_handle_track_size(output);
    missing = output->end - output->offset;
    if (missing > 1U || raw_length < missing) return false;
    proto_sum = xx_dms_checksum(decoder->raw + image_offset,
                                raw_length - missing);
    if (missing != 0U) {
        xx_dms_write_byte(output, 0U);
        if (output->failed) return false;
    }
    if (proto_sum != file_sum) {
        uint16_t fix;
        if (output->offset == 0U) return false;
        proto_sum = (uint16_t)(proto_sum - decoder->raw[output->offset - 1U]);
        fix = (uint16_t)(file_sum - proto_sum);
        if (fix >= 0x100U) return false;
        decoder->raw[output->offset - 1U] = (uint8_t)fix;
    }
    return true;
}

/* Decode one track into its slot in the image. */
static bool xx_dms_process_track(xx_dms_decoder *decoder,
                                 const xx_dms_track *track,
                                 const uint8_t *packed, size_t packed_size,
                                 size_t chunk_start, uint16_t file_sum) {
    bool do_rle;
    if (chunk_start > packed_size ||
        (size_t)track->packed_size > packed_size - chunk_start) {
        return false;
    }
    decoder->input.data = packed;
    decoder->input.offset = chunk_start;
    decoder->input.end = chunk_start + track->packed_size;
    decoder->input.failed = false;
    xx_dms_bits_reset(&decoder->bits);

    do_rle = (track->mode >= XX_DMS_MODE_QUICK &&
              track->mode <= XX_DMS_MODE_DEEP) ||
             (track->mode >= XX_DMS_MODE_HEAVY1 && (track->flags & 4U) != 0U);

    if (do_rle) {
        xx_dms_output stage;
        xx_dms_input replay;
        size_t produced;
        if ((uint32_t)track->tmp_size > decoder->tmp_size) return false;
        stage.data = decoder->tmp;
        stage.offset = 0U;
        stage.end = track->tmp_size;
        stage.failed = false;
        switch (track->mode) {
        case XX_DMS_MODE_QUICK: xx_dms_unpack_quick(decoder, &stage); break;
        case XX_DMS_MODE_MEDIUM: xx_dms_unpack_medium(decoder, &stage); break;
        case XX_DMS_MODE_DEEP: xx_dms_unpack_deep(decoder, &stage); break;
        default:
            xx_dms_unpack_heavy(decoder, &stage, (track->flags & 2U) != 0U,
                                track->mode == XX_DMS_MODE_HEAVY2);
            break;
        }
        produced = stage.offset;
        /* Whatever the LZ stage managed to produce is fed to the RLE stage;
         * a short first stage is normal, not an error. */
        decoder->input.failed = false;
        replay.data = decoder->tmp;
        replay.offset = 0U;
        replay.end = produced;
        replay.failed = false;
        decoder->output.data = decoder->raw;
        decoder->output.offset = track->image_offset;
        decoder->output.end = (size_t)track->image_offset + track->raw_size;
        decoder->output.failed = false;
        xx_dms_unrle(&decoder->output, &replay);
    } else {
        decoder->output.data = decoder->raw;
        decoder->output.offset = track->image_offset;
        decoder->output.end = (size_t)track->image_offset + track->raw_size;
        decoder->output.failed = false;
        switch (track->mode) {
        case XX_DMS_MODE_NONE: xx_dms_unpack_none(decoder, &decoder->output); break;
        case XX_DMS_MODE_SIMPLE:
            xx_dms_unrle(&decoder->output, &decoder->input);
            break;
        case XX_DMS_MODE_QUICK: xx_dms_unpack_quick(decoder, &decoder->output); break;
        case XX_DMS_MODE_MEDIUM: xx_dms_unpack_medium(decoder, &decoder->output); break;
        case XX_DMS_MODE_DEEP: xx_dms_unpack_deep(decoder, &decoder->output); break;
        default:
            xx_dms_unpack_heavy(decoder, &decoder->output,
                                (track->flags & 2U) != 0U,
                                track->mode == XX_DMS_MODE_HEAVY2);
            break;
        }
    }
    if (!xx_dms_apply_fix(decoder, &decoder->output, track->mode,
                          track->raw_size, track->image_offset, file_sum)) {
        return false;
    }
    /* Flag bit 0 keeps the LZ context alive into the next track. */
    if ((track->flags & 1U) == 0U) decoder->init_context = true;
    return true;
}

/* --- device access ------------------------------------------------------- */

static bool xx_dms_read_at(xx_io_device *device, int64_t offset, void *data,
                           size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_dms_range_within(int64_t total_size, int64_t offset,
                                int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

static void xx_dms_private_cleanup(xx_dms_private *parsed) {
    if (!parsed) return;
    if (parsed->tracks) xx_mem_free(parsed->tracks);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

/* --- parse --------------------------------------------------------------- */

static bool xx_dms_parse(Abstractformat *self, xx_dms_private *parsed,
                         xx_pd_struct *pd) {
    static const uint32_t context_sizes[XX_DMS_MODE_MAX + 1U] = {
        0U, 0U, 256U, 16384U, 16384U, 4096U, 8192U};
    uint8_t header[XX_DMS_HEADER_SIZE];
    int64_t total_size;
    int64_t available;
    int64_t offset;
    uint32_t info;
    uint32_t accounted = 0U;
    uint32_t last_track_size = 0U;
    uint32_t highest_track = 0U;
    uint32_t lowest_track = XX_DMS_TRACKS_PER_DISK;
    uint32_t previous_track = 0U;
    uint32_t track_size;
    size_t index;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_dms_range_within(total_size, self->base_address,
                             XX_DMS_HEADER_SIZE) ||
        !xx_dms_read_at(self->device, self->base_address, header,
                        sizeof(header)) ||
        xx_rt_memcmp(header, "DMS!", 4U) != 0) {
        goto fail;
    }
    parsed->input_size = total_size;
    available = total_size - self->base_address;
    /* The header CRC is the detector: four magic bytes alone are far too
     * weak, and every real encoder fills this in. */
    if (xx_dms_crc16(header + 4, 50U) !=
        xx_data_get_u16(header, sizeof(header), 54U, true)) {
        goto fail;
    }
    info = xx_data_get_u16(header, sizeof(header), 10U, true);
    parsed->is_obfuscated = (info & 2U) != 0U;
    parsed->is_hd = (info & 16U) != 0U;
    /* An MS-DOS disk inside a DMS container is not an ADF and the track
     * geometry below would not describe it. */
    if ((info & 32U) != 0U) goto fail;
    if (xx_data_get_u16(header, sizeof(header), 50U, true) > XX_DMS_MODE_MAX) {
        goto fail;
    }
    track_size = parsed->is_hd ? XX_DMS_TRACK_SIZE_HD : XX_DMS_TRACK_SIZE_DD;
    parsed->track_size = track_size;
    parsed->image_size = track_size * XX_DMS_TRACKS_PER_DISK;

    parsed->tracks = (xx_dms_track *)xx_mem_calloc(XX_DMS_MAX_TRACKS,
                                                   sizeof(*parsed->tracks));
    if (!parsed->tracks) goto fail;

    /* The header's own track numbers and sizes are advisory and frequently
     * wrong, so the real extent is found by walking the chunk chain. */
    offset = XX_DMS_HEADER_SIZE;
    while (offset + (int64_t)XX_DMS_TRACK_HEADER_SIZE < available) {
        uint8_t entry[XX_DMS_TRACK_HEADER_SIZE];
        xx_dms_track *track;
        uint32_t number;
        uint32_t packed_length;
        uint32_t raw_length;
        uint32_t tmp_length;
        uint8_t mode;
        uint8_t flags;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (parsed->count >= XX_DMS_MAX_TRACKS) break;
        if (!xx_dms_read_at(self->device, self->base_address + offset, entry,
                            sizeof(entry))) {
            goto fail;
        }
        if (entry[0] != 'T' || entry[1] != 'R') {
            /* Secondary exit: the chunk chain simply stopped. Nothing was
             * accounted for yet means this was never a DMS archive. */
            if (accounted == 0U) goto fail;
            break;
        }
        number = xx_data_get_u16(entry, sizeof(entry), 2U, true);
        /* Track numbers never go backwards in a real archive. */
        if (number < previous_track) break;
        /* 80 is the informational "disk end" chunk and anything from
         * 0x8000 up is a comment or banner block; the gap between the two
         * has no meaning and would index outside the image. */
        if (number > XX_DMS_TRACKS_PER_DISK && number < 0x8000U) goto fail;
        if (xx_dms_crc16(entry, 18U) !=
            xx_data_get_u16(entry, sizeof(entry), 18U, true)) {
            goto fail;
        }
        mode = entry[13];
        flags = entry[12];
        if (mode > XX_DMS_MODE_MAX) goto fail;
        packed_length = xx_data_get_u16(entry, sizeof(entry), 6U, true);
        tmp_length = xx_data_get_u16(entry, sizeof(entry), 8U, true);
        raw_length = xx_data_get_u16(entry, sizeof(entry), 10U, true);
        if (offset + (int64_t)XX_DMS_TRACK_HEADER_SIZE +
                (int64_t)packed_length > available) {
            goto fail;
        }
        if (context_sizes[mode] > parsed->context_size) {
            parsed->context_size = context_sizes[mode];
        }
        if (((mode >= XX_DMS_MODE_QUICK && mode <= XX_DMS_MODE_DEEP) ||
             (mode >= XX_DMS_MODE_HEAVY1 && (flags & 4U) != 0U)) &&
            tmp_length > parsed->tmp_size) {
            parsed->tmp_size = tmp_length;
        }
        track = &parsed->tracks[parsed->count];
        track->number = number;
        track->header_offset = self->base_address + offset;
        track->data_offset =
            self->base_address + offset + (int64_t)XX_DMS_TRACK_HEADER_SIZE;
        track->packed_size = packed_length;
        track->tmp_size = tmp_length;
        track->raw_size = raw_length;
        track->flags = flags;
        track->mode = mode;
        track->is_info = (number >= XX_DMS_TRACKS_PER_DISK);
        track->image_offset = 0U;
        ++parsed->count;
        if (number < XX_DMS_TRACKS_PER_DISK) {
            /* A track claiming more unpacked bytes than a physical track
             * holds is the expansion bomb this format offers; refuse it at
             * parse time rather than clipping it during extraction. */
            if (raw_length > track_size) goto fail;
            if (number >= highest_track) last_track_size = raw_length;
            if (number < lowest_track) lowest_track = number;
            if (number > highest_track) highest_track = number;
            previous_track = number;
            ++parsed->data_count;
        }
        offset += (int64_t)packed_length + (int64_t)XX_DMS_TRACK_HEADER_SIZE;
        accounted += packed_length;
        /* The real exit criterion, such as it is. */
        if (number >= XX_DMS_TRACKS_PER_DISK - 1U &&
            number < 0x8000U) {
            break;
        }
    }
    if (parsed->data_count == 0U || lowest_track > highest_track) goto fail;
    parsed->raw_offset = lowest_track * track_size;
    parsed->raw_size =
        (highest_track - lowest_track) * track_size + last_track_size;
    if (parsed->raw_size == 0U || parsed->raw_size > XX_DMS_MAX_IMAGE_SIZE ||
        parsed->raw_size > parsed->image_size) {
        goto fail;
    }
    if (offset > XX_DMS_MAX_PACKED_SIZE || offset > available) goto fail;
    parsed->packed_size = (uint32_t)offset;
    parsed->archive_end = self->base_address + offset;
    if (parsed->context_size > XX_DMS_MAX_CONTEXT_SIZE ||
        parsed->tmp_size > XX_DMS_MAX_TMP_SIZE) {
        goto fail;
    }
    /* Place every real track in the image and refuse any that would fall
     * outside it. Track numbers come straight from the file. */
    for (index = 0U; index < parsed->count; ++index) {
        xx_dms_track *track = &parsed->tracks[index];
        uint32_t position;
        if (track->is_info) continue;
        position = track->number * track_size;
        if (position < parsed->raw_offset) goto fail;
        position -= parsed->raw_offset;
        if (position > parsed->raw_size ||
            track->raw_size > parsed->raw_size - position) {
            goto fail;
        }
        track->image_offset = position;
    }
    return true;
fail:
    xx_dms_private_cleanup(parsed);
    return false;
}

/* --- decode the whole image --------------------------------------------- */

static void xx_dms_decoder_free(xx_dms_decoder *decoder) {
    if (!decoder) return;
    if (decoder->context) xx_mem_free(decoder->context);
    if (decoder->tmp) xx_mem_free(decoder->tmp);
    if (decoder->symbol_decoder.nodes) xx_mem_free(decoder->symbol_decoder.nodes);
    if (decoder->offset_decoder.nodes) xx_mem_free(decoder->offset_decoder.nodes);
    xx_mem_free(decoder);
}

/* Read the archive into memory and run every track. Returns the assembled
 * image, which the caller owns. */
static uint8_t *xx_dms_decode(Abstractformat *self,
                              const xx_dms_private *parsed,
                              xx_pd_struct *pd) {
    xx_dms_decoder *decoder;
    uint8_t *packed = NULL;
    uint8_t *raw = NULL;
    size_t index;
    bool ok = false;
    if (!self || !self->device || !parsed || parsed->is_obfuscated) return NULL;
    if (parsed->packed_size == 0U || parsed->raw_size == 0U ||
        (int64_t)parsed->packed_size > XX_DMS_MAX_PACKED_SIZE ||
        parsed->raw_size > XX_DMS_MAX_IMAGE_SIZE) {
        return NULL;
    }
    decoder = (xx_dms_decoder *)xx_mem_calloc(1U, sizeof(*decoder));
    packed = (uint8_t *)xx_mem_alloc(parsed->packed_size);
    raw = (uint8_t *)xx_mem_alloc(parsed->raw_size);
    if (!decoder || !packed || !raw) goto done;
    if (!xx_dms_read_at(self->device, self->base_address, packed,
                        parsed->packed_size)) {
        goto done;
    }
    /* Tracks the archive never mentions read back as zeros, like an
     * unwritten floppy. */
    xx_mem_zero(raw, parsed->raw_size);
    decoder->raw = raw;
    decoder->raw_size = parsed->raw_size;
    decoder->bits.input = &decoder->input;
    decoder->context_size = parsed->context_size;
    if (parsed->context_size != 0U) {
        decoder->context = (uint8_t *)xx_mem_calloc(parsed->context_size, 1U);
        if (!decoder->context) goto done;
    }
    decoder->tmp_size = parsed->tmp_size;
    if (parsed->tmp_size != 0U) {
        decoder->tmp = (uint8_t *)xx_mem_calloc(parsed->tmp_size, 1U);
        if (!decoder->tmp) goto done;
    }
    decoder->symbol_decoder.nodes = (xx_dms_huff_node *)xx_mem_calloc(
        XX_DMS_SYMBOL_NODES, sizeof(xx_dms_huff_node));
    decoder->offset_decoder.nodes = (xx_dms_huff_node *)xx_mem_calloc(
        XX_DMS_OFFSET_NODES, sizeof(xx_dms_huff_node));
    if (!decoder->symbol_decoder.nodes || !decoder->offset_decoder.nodes) {
        goto done;
    }
    decoder->symbol_decoder.capacity = XX_DMS_SYMBOL_NODES;
    decoder->offset_decoder.capacity = XX_DMS_OFFSET_NODES;
    decoder->init_context = true;
    xx_dms_vlc_init(&decoder->vlc);

    for (index = 0U; index < parsed->count; ++index) {
        const xx_dms_track *track = &parsed->tracks[index];
        size_t header_start;
        size_t chunk_start;
        uint16_t file_sum;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        /* Track 80 ends the disk; anything above 0x8000 is an informational
         * chunk that carries no disk content and is simply stepped over. */
        if (track->number == XX_DMS_TRACKS_PER_DISK) break;
        if (track->is_info) continue;
        header_start = (size_t)(track->header_offset - self->base_address);
        chunk_start = (size_t)(track->data_offset - self->base_address);
        if (header_start + XX_DMS_TRACK_HEADER_SIZE > parsed->packed_size) {
            goto done;
        }
        file_sum = (uint16_t)(((uint32_t)packed[header_start + 14U] << 8U) |
                              packed[header_start + 15U]);
        if (!xx_dms_process_track(decoder, track, packed, parsed->packed_size,
                                  chunk_start, file_sum)) {
            goto done;
        }
    }
    ok = true;
done:
    if (packed) xx_mem_free(packed);
    xx_dms_decoder_free(decoder);
    if (!ok) {
        if (raw) xx_mem_free(raw);
        return NULL;
    }
    return raw;
}

/* --- record plumbing ----------------------------------------------------- */

typedef struct xx_dms_archive_stream_s {
    xx_dms_private parsed;
    size_t index;
    uint8_t *image; /**< Decoded lazily, on the first extraction. */
} xx_dms_archive_stream;

static void xx_dms_vtable_destroy(Abstractformat *self);

static bool xx_dms_copy_options(xx_list_s *destination,
                                const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_dms_find_option(const xx_list_s *options,
                                        uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

const char *xx_dms_mode_to_string(uint32_t mode) {
    switch (mode) {
    case XX_DMS_MODE_NONE: return "NONE";
    case XX_DMS_MODE_SIMPLE: return "SIMPLE";
    case XX_DMS_MODE_QUICK: return "QUICK";
    case XX_DMS_MODE_MEDIUM: return "MEDIUM";
    case XX_DMS_MODE_DEEP: return "DEEP";
    case XX_DMS_MODE_HEAVY1: return "HEAVY1";
    case XX_DMS_MODE_HEAVY2: return "HEAVY2";
    default: return "Unknown";
    }
}

/* Record 0 is the assembled image; the rest are the tracks, in file order. */
static bool xx_dms_populate_record(xx_archive_record *record,
                                   const xx_dms_private *parsed,
                                   size_t index) {
    char name[32];
    bool folder = false;
    uint64_t uncompressed;
    uint64_t compressed;
    uint32_t mode;
    const char *method;
    if (!record || !parsed) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (index == 0U) {
        const char *image_name = "disk.adf";
        size_t length = 0U;
        while (image_name[length] != '\0' && length + 1U < sizeof(name)) {
            name[length] = image_name[length];
            ++length;
        }
        name[length] = '\0';
        record->header_offset = -1;
        record->header_size = 0;
        record->data_offset = -1;
        record->compressed_size = (int64_t)parsed->packed_size;
        uncompressed = parsed->raw_size;
        compressed = parsed->packed_size;
        /* The image is the product of every track, so no single mode names
         * it; the per-track records carry the real modes. */
        mode = 0U;
        method = "DMS";
    } else {
        const xx_dms_track *track = &parsed->tracks[index - 1U];
        /* "track_%03u" written out by hand: the CRT is off limits here. */
        uint32_t number = track->number;
        name[0] = 't'; name[1] = 'r'; name[2] = 'a'; name[3] = 'c';
        name[4] = 'k'; name[5] = '_';
        name[6] = (char)('0' + (int)((number / 10000U) % 10U));
        name[7] = (char)('0' + (int)((number / 1000U) % 10U));
        name[8] = (char)('0' + (int)((number / 100U) % 10U));
        name[9] = (char)('0' + (int)((number / 10U) % 10U));
        name[10] = (char)('0' + (int)(number % 10U));
        name[11] = '\0';
        record->header_offset = track->header_offset;
        record->header_size = XX_DMS_TRACK_HEADER_SIZE;
        record->data_offset = track->data_offset;
        record->compressed_size = (int64_t)track->packed_size;
        uncompressed = track->is_info ? 0U : track->raw_size;
        compressed = track->packed_size;
        mode = track->mode;
        method = xx_dms_mode_to_string(mode);
    }
    return xx_archive_record_set_original_name(record, name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          uncompressed) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          compressed) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          mode) &&
           xx_archive_record_set_meta_str(record, XX_META_ID_COMMENT,
                                          method) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           parsed->is_obfuscated) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           folder);
}

static void xx_dms_archive_stream_free(void *pointer) {
    xx_dms_archive_stream *stream = (xx_dms_archive_stream *)pointer;
    if (!stream) return;
    if (stream->image) xx_mem_free(stream->image);
    xx_dms_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

static bool xx_dms_safe_name(const char *name) {
    size_t index;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    for (index = 0U; name[index] != '\0'; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch < 32U || ch == ':' || ch == '<' || ch == '>' || ch == '"' ||
            ch == '|' || ch == '?' || ch == '*' || ch == '/' || ch == '\\') {
            return false;
        }
    }
    return true;
}

static bool xx_dms_write_blob(const char *path, const uint8_t *data,
                              size_t size) {
    xx_io_device *output;
    size_t done = 0U;
    bool result;
    if (!path || (!data && size != 0U)) return false;
    output = xx_io_file_open(path, "wb");
    result = output != NULL;
    while (result && done < size) {
        ssize_t sent = xx_io_write(output, data + done, size - done);
        if (sent <= 0 || (size_t)sent > size - done) {
            result = false;
            break;
        }
        done += (size_t)sent;
    }
    if (output && xx_io_close(output) != 0) result = false;
    return result;
}

/* --- public surface ------------------------------------------------------ */

void xx_dms_init(xx_dms *dms, xx_io_device *dev, int64_t base_address) {
    if (!dms) return;
    xx_mem_zero(dms, sizeof(*dms));
    xx_format_init(&dms->format, dev, base_address);
    dms->format.endian = XX_ENDIAN_BIG;
    dms->format.file_type = XX_DMS_FILE_TYPE;
    dms->format.format_type = XX_TYPE_ARCHIVE;
    dms->format.is_archive = true;
    xx_format_set_mime_type(&dms->format, "application/x-dms");
    xx_format_set_extension(&dms->format, "dms");
    dms->format.check_is_valid = xx_dms_check_is_valid;
    dms->format.handle_base_info = xx_dms_handle_base_info;
    dms->format.get_format_size = xx_dms_get_format_size;
    dms->format.get_number_of_archive_records =
        xx_dms_get_number_of_archive_records;
    dms->format.create_archive_records_reading =
        xx_dms_create_archive_records_reading;
    dms->format.get_current_archive_record = xx_dms_get_current_archive_record;
    dms->format.unpack_current_archive_record =
        xx_dms_unpack_current_archive_record;
    dms->format.archive_record_move_to_next = xx_dms_archive_record_move_to_next;
    dms->format.free_archive_records_reading =
        xx_dms_free_archive_records_reading;
    dms->format.destroy = xx_dms_vtable_destroy;
    dms->archive_end = -1;
}

xx_dms *xx_dms_create(xx_io_device *dev, int64_t base_address) {
    xx_dms *dms = (xx_dms *)xx_mem_alloc(sizeof(*dms));
    if (dms) xx_dms_init(dms, dev, base_address);
    return dms;
}

void xx_dms_destroy(xx_dms *dms) {
    if (!dms) return;
    if (dms->internal) {
        xx_dms_private_cleanup((xx_dms_private *)dms->internal);
        xx_mem_free(dms->internal);
        dms->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&dms->format);
}

static void xx_dms_vtable_destroy(Abstractformat *self) {
    xx_dms_destroy((xx_dms *)self);
}

void xx_dms_free(xx_dms *dms) {
    if (!dms) return;
    xx_dms_destroy(dms);
    xx_mem_free(dms);
}

bool xx_dms_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_dms_private parsed;
    bool result = xx_dms_parse(self, &parsed, pd);
    xx_dms_private_cleanup(&parsed);
    return result;
}

bool xx_dms_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_dms_private *parsed;
    xx_dms *dms = (xx_dms *)self;
    int64_t total_size;
    if (!self || !dms) return false;
    parsed = (xx_dms_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_dms_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (dms->internal) {
        xx_dms_private_cleanup((xx_dms_private *)dms->internal);
        xx_mem_free(dms->internal);
    }
    dms->internal = parsed;
    /* Record zero is the image itself, then one per chunk. */
    dms->number_of_records = (uint64_t)parsed->count + 1U;
    dms->number_of_members = dms->number_of_records;
    dms->number_of_tracks = parsed->data_count;
    dms->image_size = parsed->image_size;
    dms->raw_size = parsed->raw_size;
    dms->raw_offset = parsed->raw_offset;
    dms->is_hd = parsed->is_hd;
    dms->is_obfuscated = parsed->is_obfuscated;
    dms->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    self->is_crypted = parsed->is_obfuscated;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = dms->number_of_records;
    xx_format_set_version(self, parsed->is_hd ? "HD" : "DD");
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_dms_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return -1;
    }
    return self->format_size;
}

uint64_t xx_dms_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return ((xx_dms *)self)->number_of_records;
}

xx_archive_record_state *xx_dms_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_dms_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_dms_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_dms_copy_options(&state->options, options) ||
        !xx_dms_parse(self, &stream->parsed, pd)) {
        xx_dms_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_dms_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count + 1;
    if (xx_dms_populate_record(&state->current_record, &stream->parsed, 0U)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_dms_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_dms_archive_record_move_to_next(Abstractformat *self,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    xx_dms_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dms_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index > stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_dms_populate_record(&state->current_record, &stream->parsed,
                               stream->index)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_dms_unpack_current_archive_record(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_dms_archive_stream *stream;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    const uint8_t *blob;
    size_t blob_size;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_dms_archive_stream *)state->internal_state;
    if (stream->index > stream->parsed.count) return false;
    /* Obfuscated archives are listed but never decoded: finding the key
     * means brute-forcing a 17-bit space with a full image decode per
     * candidate, which is compute an attacker gets to spend for free. */
    if (stream->parsed.is_obfuscated) return false;
    if (stream->index != 0U &&
        stream->parsed.tracks[stream->index - 1U].is_info) {
        /* An informational chunk holds no disk bytes; there is nothing to
         * write, and reporting success would be a lie. */
        return false;
    }
    if (!stream->image) {
        stream->image = xx_dms_decode(self, &stream->parsed, pd);
        if (!stream->image) return false;
    }
    if (stream->index == 0U) {
        blob = stream->image;
        blob_size = stream->parsed.raw_size;
    } else {
        const xx_dms_track *track = &stream->parsed.tracks[stream->index - 1U];
        if ((size_t)track->image_offset > stream->parsed.raw_size ||
            track->raw_size >
                stream->parsed.raw_size - (size_t)track->image_offset) {
            return false;
        }
        blob = stream->image + track->image_offset;
        blob_size = track->raw_size;
    }
    option = xx_dms_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return true; /* Decoded and discarded: the record verifies. */
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    {
        const char *name = xx_archive_record_get_original_name(
            &state->current_record);
        if (!xx_dms_safe_name(name)) goto cleanup;
        if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\') {
            destination = xx_str_concat3(base, "/", name);
        } else {
            destination = xx_str_concat(base, name);
        }
    }
    if (!destination) goto cleanup;
    result = xx_store_create_dirs_a(destination, false) &&
             xx_dms_write_blob(destination, blob, blob_size);
    if (!result) xx_rt_remove(destination);
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_dms_free_archive_records_reading(Abstractformat *self,
                                         xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_dms_get_number_of_records(const xx_dms *dms) {
    return dms ? dms->number_of_records : 0U;
}
uint64_t xx_dms_get_number_of_members(const xx_dms *dms) {
    return dms ? dms->number_of_members : 0U;
}
uint64_t xx_dms_get_number_of_tracks(const xx_dms *dms) {
    return dms ? dms->number_of_tracks : 0U;
}
uint32_t xx_dms_get_image_size(const xx_dms *dms) {
    return dms ? dms->image_size : 0U;
}
uint32_t xx_dms_get_raw_size(const xx_dms *dms) {
    return dms ? dms->raw_size : 0U;
}
int64_t xx_dms_get_archive_end(const xx_dms *dms) {
    return dms ? dms->archive_end : -1;
}
