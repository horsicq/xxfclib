/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DiskDoubler member stream decoders (methods 1, 6/9 and 10), ported from
 * XArchive/Algos/xmaclegacydecoders.cpp (decodeDiskDoublerADn,
 * decodeDiskDoublerDDn, decodeDiskDoublerLZW) which is itself a translation of
 * the XADMaster legacy Macintosh handlers, plus
 * XArchive/Algos/xborlandpackdecoder.cpp for the headerless Unix-compress core
 * the LZW method wraps.
 *
 * Method 8 (Compact Pro LZH) is deliberately absent: it is the very same
 * algorithm the Compact Pro archive reader uses, and lives in the compactpro
 * module.  This file depends on nothing but the runtime helpers.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/diskdoubler/xx_diskdoubler.h"

/* ------------------------------------------------------------ bit reader ---
 * MSB-first reader used by the ADn and DDn methods.  It is bounded by the
 * whole packed fork rather than by the current block: the reference reader is
 * constructed over the entire buffer and a block's bit stream is allowed to
 * read past its own compressed extent.  That tolerance is kept deliberately --
 * it never escapes the input buffer, and tightening it would reject streams
 * the reference accepts.
 */
typedef struct dd_bits {
    const uint8_t *data;
    size_t size;
    uint64_t bit;
    bool ok;
} dd_bits;

static void dd_bits_init(dd_bits *reader, const uint8_t *data, size_t size,
                         size_t byte_offset) {
    reader->data = data;
    reader->size = size;
    reader->bit = (uint64_t)byte_offset * 8U;
    reader->ok = byte_offset <= size;
}

static uint32_t dd_bits_read(dd_bits *reader, unsigned count) {
    uint32_t value = 0U;
    unsigned i;
    if (!reader->ok || count > 32U ||
        reader->bit + (uint64_t)count > (uint64_t)reader->size * 8U) {
        reader->ok = false;
        return 0U;
    }
    for (i = 0U; i < count; ++i) {
        uint64_t bit = reader->bit++;
        value = (value << 1) |
                (uint32_t)((reader->data[(size_t)(bit >> 3)] >>
                            (7U - (unsigned)(bit & 7U))) & 1U);
    }
    return value;
}

/* ---------------------------------------------------------- prefix codes ---
 * A literal port of the reference PrefixCode: codes are handed out in
 * canonical order (shortest length first, symbol order within a length) and
 * inserted bit by bit into a binary tree.  A tree, not a table, so that the
 * reference's exact rejection points survive -- notably the early "return
 * true" once the last non-zero length has been placed, which accepts an
 * incomplete code that a table builder would reject.
 *
 * Node 0 is the root and is never anyone's child, so child index 0 doubles as
 * "absent".  At most 256 symbols of at most 31 bits can be inserted, which
 * bounds the interior nodes at 256*31 plus the root.
 */
#define DD_MAX_NODES 7937

typedef struct dd_prefix {
    uint16_t child[DD_MAX_NODES][2];
    int16_t symbol[DD_MAX_NODES];
    int32_t used;
} dd_prefix;

static void dd_prefix_reset(dd_prefix *code) {
    code->used = 1;
    code->child[0][0] = 0U;
    code->child[0][1] = 0U;
    code->symbol[0] = -1;
}

static bool dd_prefix_build(dd_prefix *code, const int32_t *lengths,
                            int32_t count, int32_t maximum_length) {
    uint32_t value = 0U;
    int32_t symbols_left = 0;
    int32_t length, index;

    dd_prefix_reset(code);
    for (index = 0; index < count; ++index) {
        if (lengths[index] > 0) ++symbols_left;
    }
    if (!symbols_left || maximum_length <= 0 || maximum_length > 31) {
        return false;
    }

    for (length = 1; length <= maximum_length; ++length) {
        for (index = 0; index < count; ++index) {
            int32_t node = 0;
            int32_t bit_position;
            if (lengths[index] != length) continue;
            if (value >= ((uint32_t)1 << length)) return false;
            for (bit_position = length - 1; bit_position >= 0;
                 --bit_position) {
                int32_t bit, next;
                if (code->symbol[node] >= 0) return false;
                bit = (int32_t)((value >> bit_position) & 1U);
                next = (int32_t)code->child[node][bit];
                if (next == 0) {
                    if (code->used >= DD_MAX_NODES) return false;
                    next = code->used++;
                    code->child[node][bit] = (uint16_t)next;
                    code->child[next][0] = 0U;
                    code->child[next][1] = 0U;
                    code->symbol[next] = -1;
                }
                node = next;
            }
            if (code->symbol[node] >= 0 || code->child[node][0] != 0U ||
                code->child[node][1] != 0U) {
                return false;
            }
            code->symbol[node] = (int16_t)index;
            ++value;
            if (--symbols_left == 0) return true;
        }
        value <<= 1;
    }
    return symbols_left == 0;
}

static int32_t dd_prefix_symbol(const dd_prefix *code, dd_bits *reader) {
    int32_t node = 0;
    int32_t depth;
    for (depth = 0; depth <= 31; ++depth) {
        uint32_t bit;
        if (code->symbol[node] >= 0) return code->symbol[node];
        bit = dd_bits_read(reader, 1U);
        if (!reader->ok) return -1;
        node = (int32_t)code->child[node][bit];
        if (node == 0 || node >= code->used) return -1;
    }
    return -1;
}

static uint32_t dd_be16(const uint8_t *data, size_t offset) {
    return ((uint32_t)data[offset] << 8) | (uint32_t)data[offset + 1U];
}

static uint32_t dd_be32(const uint8_t *data, size_t offset) {
    return (dd_be16(data, offset) << 16) | dd_be16(data, offset + 2U);
}

/* ============================================================== ADn (6/9) */

bool xx_diskdoubler_adn_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written) {
    size_t position = 0U;
    size_t produced = 0U;

    if (written) *written = 0U;
    if ((!input && input_size) || (!output && output_size)) return false;

    while (produced < output_size) {
        uint32_t compressed_size, uncompressed_size;
        uint8_t header_xor, flags;
        size_t data_start, next_block;
        int i;

        if (input_size < 12U || position > input_size - 12U) return false;
        compressed_size = dd_be16(input, position);
        uncompressed_size = dd_be16(input, position + 2U);
        if (!compressed_size || !uncompressed_size ||
            uncompressed_size > 0x2000U ||
            compressed_size > input_size ||
            position + 12U > input_size - compressed_size ||
            uncompressed_size > output_size - produced) {
            return false;
        }

        header_xor = (uint8_t)(compressed_size ^ (compressed_size >> 8) ^
                               uncompressed_size ^ (uncompressed_size >> 8));
        for (i = 4; i < 11; ++i) {
            header_xor ^= input[position + (size_t)i];
        }
        if (header_xor != input[position + 11U]) return false;

        flags = input[position + 9U];
        data_start = position + 12U;
        next_block = data_start + compressed_size;

        if (flags & 1U) {
            /* Stored block.  The reference insists the stored extent is at
             * least as large as the plaintext even though only the first
             * uncompressed_size bytes are used. */
            if (compressed_size < uncompressed_size) return false;
            xx_rt_memcpy(output + produced, input + data_start,
                         uncompressed_size);
            produced += uncompressed_size;
        } else {
            dd_bits reader;
            uint8_t *block = output + produced;
            size_t block_pos = 0U;
            dd_bits_init(&reader, input, input_size, data_start);
            while (block_pos < uncompressed_size) {
                if (!dd_bits_read(&reader, 1U)) {
                    uint32_t literal = dd_bits_read(&reader, 8U);
                    if (!reader.ok) return false;
                    block[block_pos++] = (uint8_t)literal;
                } else {
                    uint32_t far_offset = dd_bits_read(&reader, 1U);
                    uint32_t offset = dd_bits_read(&reader,
                                                   far_offset ? 12U : 8U);
                    uint32_t length;
                    size_t copy;
                    if (!dd_bits_read(&reader, 1U)) {
                        length = 2U;
                    } else if (!dd_bits_read(&reader, 1U)) {
                        length = dd_bits_read(&reader, 1U) ? 4U : 3U;
                    } else {
                        length = dd_bits_read(&reader, 4U) + 5U;
                    }
                    /* Back-references never leave the current block: the
                     * reference bounds the offset by the block's own output,
                     * not by the whole fork. */
                    if (!reader.ok || offset == 0U ||
                        (size_t)offset > block_pos || length > offset) {
                        return false;
                    }
                    copy = length;
                    if (copy > uncompressed_size - block_pos) {
                        copy = uncompressed_size - block_pos;
                    }
                    while (copy--) {
                        block[block_pos] = block[block_pos - offset];
                        ++block_pos;
                    }
                }
                if (!reader.ok) return false;
            }
            produced += uncompressed_size;
        }
        position = next_block;
    }

    if (written) *written = produced;
    return produced == output_size;
}

/* ================================================================ DDn (10) */

typedef struct ddn_scratch {
    dd_prefix code;
    uint16_t offsets[65536];
    uint8_t literals[65536];
} ddn_scratch;

/* Reads one DDn Huffman table description.  Returns the offset just past the
 * table, which the caller checks against the start of the next stream. */
static bool ddn_read_code(const uint8_t *input, size_t input_size,
                          size_t start, dd_prefix *code, int32_t *lengths,
                          size_t *end_offset) {
    uint32_t header;
    int32_t code_count, byte_count, maximum_length, bit_count, i;
    bool zero_coding;
    dd_bits reader;

    if (input_size < 4U || start > input_size - 4U) return false;
    header = dd_be32(input, start);
    code_count = (int32_t)((header >> 24) & 0xffU) + 1;
    byte_count = (int32_t)((header >> 13) & 0x7ffU);
    maximum_length = (int32_t)((header >> 8) & 0x1fU);
    bit_count = (int32_t)((header >> 3) & 0x1fU);
    if (code_count <= 0 || code_count > 256 ||
        (size_t)byte_count > input_size ||
        start + 4U > input_size - (size_t)byte_count ||
        maximum_length <= 0 || maximum_length > 31 ||
        bit_count <= 0 || bit_count > 16) {
        return false;
    }

    dd_bits_init(&reader, input, input_size, start + 4U);
    zero_coding = (header & 4U) != 0U;
    for (i = 0; i < code_count; ++i) {
        lengths[i] = 0;
        if (!zero_coding || dd_bits_read(&reader, 1U)) {
            lengths[i] = (int32_t)dd_bits_read(&reader, (unsigned)bit_count);
        }
        if (!reader.ok || lengths[i] > maximum_length) return false;
    }
    if (!dd_prefix_build(code, lengths, code_count, maximum_length)) {
        return false;
    }
    *end_offset = start + 4U + (size_t)byte_count;
    return true;
}

bool xx_diskdoubler_ddn_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written) {
    ddn_scratch *scratch;
    int32_t lengths[256];
    size_t next_block = 0U;
    size_t produced = 0U;
    bool result = true;

    if (written) *written = 0U;
    if ((!input && input_size) || (!output && output_size)) return false;
    if (output_size == 0U) return true;

    scratch = (ddn_scratch *)xx_mem_alloc(sizeof(ddn_scratch));
    if (!scratch) return false;

    while (result && produced < output_size) {
        size_t start = next_block;
        uint32_t uncompressed_size;
        uint32_t literal_count, offset_count;
        uint32_t length_packed, literal_packed, offset_packed;
        uint8_t header_xor = 0U;
        uint8_t flags;
        size_t data_start, literal_start, length_start, code_end, block_end;
        uint32_t literal_index = 0U;
        uint32_t offset_index = 0U;
        dd_bits reader;
        int i;

        if (input_size < 22U || start > input_size - 22U) {
            result = false;
            break;
        }
        uncompressed_size = dd_be32(input, start);
        literal_count = dd_be16(input, start + 4U);
        offset_count = dd_be16(input, start + 6U);
        length_packed = dd_be16(input, start + 8U);
        literal_packed = dd_be16(input, start + 10U);
        offset_packed = dd_be16(input, start + 12U);
        if (!uncompressed_size || uncompressed_size > 65536U ||
            (size_t)uncompressed_size > output_size - produced) {
            result = false;
            break;
        }
        for (i = 0; i < 21; ++i) header_xor ^= input[start + (size_t)i];
        if (header_xor != input[start + 21U]) {
            result = false;
            break;
        }
        flags = input[start + 14U];
        data_start = start + 22U;

        if (flags & 0x40U) {
            next_block = data_start + uncompressed_size;
            if (next_block > input_size) {
                result = false;
                break;
            }
            xx_rt_memcpy(output + produced, input + data_start,
                         uncompressed_size);
            produced += uncompressed_size;
            continue;
        }

        /* Stream order on disk is offsets, literals, lengths; the header
         * gives each one's packed size. */
        literal_start = data_start + offset_packed;
        length_start = literal_start + literal_packed;
        next_block = length_start + length_packed;
        if (next_block > input_size) {
            result = false;
            break;
        }

        if (!ddn_read_code(input, input_size, data_start, &scratch->code,
                           lengths, &code_end) ||
            code_end > literal_start) {
            result = false;
            break;
        }
        dd_bits_init(&reader, input, input_size, code_end);
        for (i = 0; result && i < (int)offset_count; ++i) {
            int32_t slot = dd_prefix_symbol(&scratch->code, &reader);
            uint32_t offset;
            if (slot < 0 || slot > 31) {
                result = false;
                break;
            }
            if (slot < 4) {
                offset = (uint32_t)slot + 1U;
            } else {
                unsigned extra = (unsigned)(slot / 2 - 1);
                uint32_t base = ((2U + (uint32_t)(slot & 1)) << extra) + 1U;
                offset = base + dd_bits_read(&reader, extra);
            }
            if (!reader.ok || offset == 0U || offset > 65535U) {
                result = false;
                break;
            }
            scratch->offsets[i] = (uint16_t)offset;
        }
        if (!result) break;

        if (flags & 0x80U) {
            if (!ddn_read_code(input, input_size, literal_start,
                               &scratch->code, lengths, &code_end) ||
                code_end > length_start) {
                result = false;
                break;
            }
            dd_bits_init(&reader, input, input_size, code_end);
            for (i = 0; i < (int)literal_count; ++i) {
                int32_t symbol = dd_prefix_symbol(&scratch->code, &reader);
                if (symbol < 0 || symbol > 255) {
                    result = false;
                    break;
                }
                scratch->literals[i] = (uint8_t)symbol;
            }
            if (!result) break;
        } else {
            if ((size_t)literal_count > input_size ||
                literal_start > input_size - (size_t)literal_count ||
                literal_start + literal_count > length_start) {
                result = false;
                break;
            }
            xx_rt_memcpy(scratch->literals, input + literal_start,
                         literal_count);
        }

        if (!ddn_read_code(input, input_size, length_start, &scratch->code,
                           lengths, &code_end) ||
            code_end > next_block) {
            result = false;
            break;
        }
        dd_bits_init(&reader, input, input_size, code_end);
        block_end = produced + uncompressed_size;
        while (produced < block_end) {
            int32_t symbol = dd_prefix_symbol(&scratch->code, &reader);
            if (symbol < 0) {
                result = false;
                break;
            }
            if (symbol == 0) {
                if (literal_index >= literal_count) {
                    result = false;
                    break;
                }
                output[produced++] = scratch->literals[literal_index++];
            } else if (symbol < 128) {
                size_t length = (size_t)symbol + 2U;
                uint32_t offset;
                if (offset_index >= offset_count) {
                    result = false;
                    break;
                }
                offset = scratch->offsets[offset_index++];
                /* The window is the whole output, not just this block. */
                if (offset == 0U || (size_t)offset > produced) {
                    result = false;
                    break;
                }
                if (length > block_end - produced) {
                    length = block_end - produced;
                }
                while (length--) {
                    output[produced] = output[produced - offset];
                    ++produced;
                }
            } else {
                size_t length;
                if (symbol - 128 > 16) {
                    result = false;
                    break;
                }
                length = (size_t)1U << (symbol - 128);
                if (length > block_end - produced) {
                    length = block_end - produced;
                }
                if (length > literal_count ||
                    literal_index > literal_count - length) {
                    result = false;
                    break;
                }
                xx_rt_memcpy(output + produced,
                             scratch->literals + literal_index, length);
                produced += length;
                literal_index += (uint32_t)length;
            }
        }
    }

    xx_mem_free(scratch);
    if (!result || produced != output_size) return false;
    if (written) *written = produced;
    return true;
}

/* ================================================================ LZW (1) */

#define DD_LZW_CLEAR 256
#define DD_LZW_FIRST 257
#define DD_LZW_MINBITS 9
#define DD_LZW_MAXBITS 16

typedef struct dd_lzw_bits {
    const uint8_t *data;
    uint64_t total_bits;
    uint64_t position;
} dd_lzw_bits;

/* LSB-first, the Unix-compress packing.  Bit 0 is the low bit of the first
 * code byte, which is the byte right after the one-byte flags header. */
static int32_t dd_lzw_read(dd_lzw_bits *reader, int32_t code_bits) {
    int32_t code = 0;
    int32_t i;
    if (code_bits < 1 || code_bits > DD_LZW_MAXBITS ||
        reader->position + (uint64_t)code_bits > reader->total_bits) {
        return -1;
    }
    for (i = 0; i < code_bits; ++i) {
        uint64_t bit = reader->position + (uint64_t)i;
        code |= (int32_t)((reader->data[(size_t)(bit >> 3)] >>
                           (unsigned)(bit & 7U)) & 1U) << i;
    }
    reader->position += (uint64_t)code_bits;
    return code;
}

static bool dd_lzw_skip(dd_lzw_bits *reader, uint64_t count) {
    if (reader->position + count > reader->total_bits) return false;
    reader->position += count;
    return true;
}

/* compress emits codes in groups of eight.  A width change or a CLEAR leaves
 * the tail of the current group as padding; the next code starts at the
 * following group boundary.  Deliberate: dropping this desynchronises every
 * real .Z stream. */
static bool dd_lzw_align(dd_lzw_bits *reader, int32_t code_bits,
                         uint64_t *group_start) {
    uint64_t group_bits, used_bits, skip_bits;
    if (code_bits < DD_LZW_MINBITS || code_bits > DD_LZW_MAXBITS ||
        reader->position < *group_start) {
        return false;
    }
    group_bits = (uint64_t)code_bits * 8U;
    used_bits = reader->position - *group_start;
    skip_bits = (group_bits - (used_bits % group_bits)) % group_bits;
    if (!dd_lzw_skip(reader, skip_bits)) return false;
    *group_start = reader->position;
    return true;
}

typedef struct dd_lzw_tables {
    uint16_t *prefix; /* own allocation */
    uint8_t *bytes;   /* own allocation: suffix then stack */
    uint8_t *suffix;
    uint8_t *stack;
} dd_lzw_tables;

static void dd_lzw_tables_free(dd_lzw_tables *tables) {
    xx_mem_free(tables->prefix);
    xx_mem_free(tables->bytes);
    tables->prefix = NULL;
    tables->bytes = NULL;
}

bool xx_diskdoubler_lzw_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t info1, uint8_t info2,
                                      uint16_t checksum, uint8_t *output,
                                      size_t output_size, size_t *written) {
    uint8_t xor_byte, magic1, magic2, flags;
    int32_t max_bits, max_code, next_code, code_bits, max_value;
    int32_t old_code;
    uint8_t final_char;
    size_t produced = 0U;
    uint64_t group_start = 0U;
    uint32_t sum;
    dd_lzw_bits reader;
    dd_lzw_tables tables;
    bool result = true;
    int32_t i;

    tables.prefix = NULL;
    tables.bytes = NULL;
    tables.suffix = NULL;
    tables.stack = NULL;

    if (written) *written = 0U;
    if ((!input && input_size) || (!output && output_size)) return false;
    if (input_size < 3U) return false;

    /* Generation 0x2a and later mask the stream with 0x5a unless the second
     * info byte says otherwise.  The mask covers only the three-byte .Z
     * header and the *decoded* plaintext -- the code stream itself is clean. */
    xor_byte = (info1 >= 0x2aU && !(info2 & 0x80U)) ? 0x5aU : 0x00U;
    magic1 = (uint8_t)(input[0] ^ xor_byte);
    magic2 = (uint8_t)(input[1] ^ xor_byte);
    flags = (uint8_t)(input[2] ^ xor_byte);
    if (magic1 != 0x1fU || magic2 != 0x9dU) return false;

    /* Bits 5 and 6 of the flags byte are reserved and must be zero. */
    if (flags & 0x60U) return false;
    max_bits = (int32_t)(flags & 0x1fU);
    if (max_bits < DD_LZW_MINBITS || max_bits > DD_LZW_MAXBITS) return false;
    max_code = (int32_t)1 << max_bits;

    tables.prefix =
        (uint16_t *)xx_mem_alloc((size_t)max_code * sizeof(uint16_t));
    tables.bytes = (uint8_t *)xx_mem_alloc((size_t)max_code * 2U);
    if (!tables.prefix || !tables.bytes) {
        dd_lzw_tables_free(&tables);
        return false;
    }
    tables.suffix = tables.bytes;
    tables.stack = tables.bytes + (size_t)max_code;
    xx_rt_memset(tables.prefix, 0, (size_t)max_code * sizeof(uint16_t));
    xx_rt_memset(tables.bytes, 0, (size_t)max_code * 2U);
    for (i = 0; i < 256; ++i) tables.suffix[i] = (uint8_t)i;

    reader.data = input + 3;
    reader.total_bits = (uint64_t)(input_size - 3U) * 8U;
    reader.position = 0U;
    next_code = (flags & 0x80U) ? DD_LZW_FIRST : 256;
    code_bits = DD_LZW_MINBITS;
    max_value = (int32_t)1 << code_bits;

    /* The first code addresses a pristine dictionary, so it can only be a
     * literal; a forward code here would read uninitialised table entries. */
    old_code = dd_lzw_read(&reader, code_bits);
    if (old_code < 0 || old_code >= 256) {
        bool empty_ok = (old_code < 0) && (output_size == 0U);
        dd_lzw_tables_free(&tables);
        if (!empty_ok) return false;
        sum = (uint32_t)magic1 + magic2 + flags;
        if ((sum & 0xffffU) != checksum) return false;
        if (written) *written = 0U;
        return true;
    }
    final_char = (uint8_t)old_code;
    if (produced >= output_size) {
        dd_lzw_tables_free(&tables);
        return false;
    }
    output[produced++] = final_char;

    while (result) {
        int32_t code = dd_lzw_read(&reader, code_bits);
        int32_t in_code, stack_top;
        if (code < 0) break; /* End of stream. */

        if ((flags & 0x80U) && code == DD_LZW_CLEAR) {
            do {
                if (!dd_lzw_align(&reader, code_bits, &group_start)) {
                    result = false;
                    break;
                }
                next_code = DD_LZW_FIRST;
                code_bits = DD_LZW_MINBITS;
                max_value = (int32_t)1 << code_bits;
                code = dd_lzw_read(&reader, code_bits);
            } while (code == DD_LZW_CLEAR);
            if (!result || code < 0 || code >= 256) {
                result = false;
                break;
            }
            old_code = code;
            final_char = (uint8_t)code;
            if (produced >= output_size) {
                result = false;
                break;
            }
            output[produced++] = final_char;
            continue;
        }

        in_code = code;
        stack_top = 0;

        /* KwKwK: the encoder may reference the entry it is about to add. */
        if (code >= next_code) {
            if (code > next_code || code >= max_code) {
                result = false;
                break;
            }
            tables.stack[stack_top++] = final_char;
            code = old_code;
        }

        while (code >= 256) {
            if (code >= next_code || code >= max_code ||
                stack_top >= max_code) {
                result = false;
                break;
            }
            tables.stack[stack_top++] = tables.suffix[code];
            code = (int32_t)tables.prefix[code];
        }
        if (!result) break;

        if (code < 0 || code >= 256 || stack_top >= max_code) {
            result = false;
            break;
        }
        final_char = tables.suffix[code];
        tables.stack[stack_top++] = final_char;

        if ((size_t)stack_top > output_size ||
            produced > output_size - (size_t)stack_top) {
            result = false;
            break;
        }
        for (i = stack_top - 1; i >= 0; --i) {
            output[produced++] = tables.stack[i];
        }

        if (next_code < max_code) {
            tables.prefix[next_code] = (uint16_t)old_code;
            tables.suffix[next_code] = final_char;
            ++next_code;
            if (next_code >= max_value && code_bits < max_bits) {
                if (!dd_lzw_align(&reader, code_bits, &group_start)) {
                    result = false;
                    break;
                }
                ++code_bits;
                max_value = (int32_t)1 << code_bits;
            }
        }

        old_code = in_code;
    }

    dd_lzw_tables_free(&tables);
    if (!result || produced != output_size) return false;

    /* The additive checksum covers the three unmasked header bytes and every
     * unmasked plaintext byte, modulo 2^16. */
    sum = (uint32_t)magic1 + magic2 + flags;
    for (produced = 0U; produced < output_size; ++produced) {
        uint8_t value = (uint8_t)(output[produced] ^ xor_byte);
        output[produced] = value;
        sum = (sum + value) & 0xffffU;
    }
    if (sum != checksum) return false;

    if (written) *written = output_size;
    return true;
}
