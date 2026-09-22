/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Port of the HANDLE_METHOD_IVT arm of XArchive/core/xdecompress.cpp (the
 * framing walk) together with decInflateMSZIPBlock() (the per-block inflate).
 *
 * The reference has no inflateSetDictionary, so it feeds the previous 32 KiB
 * back in as a non-final stored block and then strips those bytes off the
 * result.  That is a workaround for a device-to-device inflater, not part of
 * the format: decoding into one contiguous output buffer and simply allowing a
 * back-reference to reach up to 32768 bytes behind the current block start is
 * output-equivalent, byte for byte, and is what this does.  The bounded
 * distance is the same bound the stored-block trick produces, since the
 * reference caps the injected dictionary at 32768 bytes as well.
 */
#include "xxfclib/algo/ivt/xx_ivt.h"

#define IVT_HEADER_SIZE 12U
#define IVT_MAX_BLOCK 32768U
#define IVT_WINDOW 32768U
#define IVT_MAX_BITS 15U

typedef struct ivt_bits {
    const uint8_t *input;
    size_t input_size;
    size_t offset;
    uint32_t bit_buffer;
    unsigned bit_count;
} ivt_bits;

typedef struct ivt_huff {
    uint16_t count[IVT_MAX_BITS + 1U];
    uint16_t symbol[288];
} ivt_huff;

/* RFC 1951 3.2.5 */
static const uint16_t ivt_length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43,
    51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t ivt_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3,
    3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t ivt_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
    513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t ivt_dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
    8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const uint8_t ivt_clen_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

/* LSB-first, as DEFLATE specifies. */
static bool ivt_get_bits(ivt_bits *reader, unsigned need, uint32_t *value)
{
    while (reader->bit_count < need) {
        if (reader->offset >= reader->input_size) return false;
        reader->bit_buffer |=
            (uint32_t)reader->input[reader->offset++] << reader->bit_count;
        reader->bit_count += 8U;
    }

    *value = (need == 0U) ? 0U : (reader->bit_buffer & ((1U << need) - 1U));
    reader->bit_buffer >>= need;
    reader->bit_count -= need;

    return true;
}

/* Canonical Huffman, built from the code lengths exactly as RFC 1951 3.2.2
 * defines it; the counts/symbols layout differs from zlib's internal table but
 * decodes the identical code, which is what output equivalence needs. */
static int ivt_build(ivt_huff *table, const uint8_t *lengths, unsigned symbols)
{
    unsigned index;
    unsigned length;
    unsigned left;
    uint16_t offsets[IVT_MAX_BITS + 2U];

    for (length = 0U; length <= IVT_MAX_BITS; ++length) table->count[length] = 0U;
    for (index = 0U; index < symbols; ++index) table->count[lengths[index]]++;

    /* Over-subscribed is always fatal; incomplete is reported so the caller
     * can allow the cases DEFLATE permits (an empty or single-code distance
     * tree).  An empty table decodes nothing, which is the correct behaviour
     * for a block that contains no matches at all. */
    left = 1U;
    for (length = 1U; length <= IVT_MAX_BITS; ++length) {
        left <<= 1;
        if (table->count[length] > left) return -1;
        left -= table->count[length];
    }

    offsets[1] = 0U;
    for (length = 1U; length < IVT_MAX_BITS + 1U; ++length) {
        offsets[length + 1U] = (uint16_t)(offsets[length] + table->count[length]);
    }
    for (index = 0U; index < symbols; ++index) {
        if (lengths[index] != 0U) {
            table->symbol[offsets[lengths[index]]++] = (uint16_t)index;
        }
    }

    return (int)left;
}

static int ivt_decode_symbol(ivt_bits *reader, const ivt_huff *table)
{
    unsigned length;
    unsigned code = 0U;
    unsigned first = 0U;
    unsigned index = 0U;
    unsigned count;
    uint32_t bit;

    for (length = 1U; length <= IVT_MAX_BITS; ++length) {
        if (!ivt_get_bits(reader, 1U, &bit)) return -1;
        code |= (unsigned)bit;
        count = table->count[length];
        if ((code - first) < count) {
            return (int)table->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }

    return -1;
}

typedef struct ivt_sink {
    uint8_t *output;
    size_t position;   /* total bytes produced so far, history included */
    size_t block_end;  /* position at which this block must stop */
} ivt_sink;

static bool ivt_put(ivt_sink *sink, uint8_t value)
{
    if (sink->position >= sink->block_end) return false;
    sink->output[sink->position++] = value;
    return true;
}

/* Decodes one raw DEFLATE stream - MSZIP blocks always consist of a single
 * stream ending in a final block - into sink, which already holds the previous
 * blocks' output as the dictionary. */
static bool ivt_inflate(ivt_bits *reader, ivt_sink *sink)
{
    ivt_huff literals;
    ivt_huff distances;
    uint8_t lengths[320];
    uint32_t value;
    uint32_t final_block;
    uint32_t type;
    unsigned index;
    unsigned hlit;
    unsigned hdist;
    unsigned hclen;
    unsigned extra;
    unsigned repeat;
    size_t source;
    size_t max_distance;
    int symbol;
    int err;
    uint32_t length;
    uint32_t distance;

    for (;;) {
        if (!ivt_get_bits(reader, 1U, &final_block)) return false;
        if (!ivt_get_bits(reader, 2U, &type)) return false;

        if (type == 0U) {
            uint32_t stored_len;
            uint32_t stored_nlen;
            /* Discard the partial byte and read LEN/NLEN. */
            reader->bit_buffer = 0U;
            reader->bit_count = 0U;
            if ((reader->input_size - reader->offset) < 4U) return false;
            stored_len = (uint32_t)reader->input[reader->offset] |
                         ((uint32_t)reader->input[reader->offset + 1U] << 8);
            stored_nlen = (uint32_t)reader->input[reader->offset + 2U] |
                          ((uint32_t)reader->input[reader->offset + 3U] << 8);
            reader->offset += 4U;
            if (stored_len != ((~stored_nlen) & 0xFFFFU)) return false;
            if ((reader->input_size - reader->offset) < (size_t)stored_len) return false;
            for (index = 0U; index < (unsigned)stored_len; ++index) {
                if (!ivt_put(sink, reader->input[reader->offset++])) return false;
            }
        } else if ((type == 1U) || (type == 2U)) {
            if (type == 1U) {
                /* Fixed trees, rebuilt into locals: no module-level state. */
                for (index = 0U; index < 144U; ++index) lengths[index] = 8U;
                for (; index < 256U; ++index) lengths[index] = 9U;
                for (; index < 280U; ++index) lengths[index] = 7U;
                for (; index < 288U; ++index) lengths[index] = 8U;
                if (ivt_build(&literals, lengths, 288U) != 0) return false;
                /* 32 five-bit distance codes, not 30: the fixed tree is
                 * complete and symbols 30/31 exist but are invalid, which the
                 * match decoder below rejects. */
                for (index = 0U; index < 32U; ++index) lengths[index] = 5U;
                if (ivt_build(&distances, lengths, 32U) != 0) return false;
            } else {
                if (!ivt_get_bits(reader, 5U, &value)) return false;
                hlit = (unsigned)value + 257U;
                if (!ivt_get_bits(reader, 5U, &value)) return false;
                hdist = (unsigned)value + 1U;
                if (!ivt_get_bits(reader, 4U, &value)) return false;
                hclen = (unsigned)value + 4U;
                if ((hlit > 286U) || (hdist > 30U)) return false;

                for (index = 0U; index < 19U; ++index) lengths[index] = 0U;
                for (index = 0U; index < hclen; ++index) {
                    if (!ivt_get_bits(reader, 3U, &value)) return false;
                    lengths[ivt_clen_order[index]] = (uint8_t)value;
                }
                if (ivt_build(&literals, lengths, 19U) != 0) return false;

                index = 0U;
                while (index < (hlit + hdist)) {
                    symbol = ivt_decode_symbol(reader, &literals);
                    if (symbol < 0) return false;
                    if (symbol < 16) {
                        lengths[index++] = (uint8_t)symbol;
                    } else {
                        uint8_t previous = 0U;
                        if (symbol == 16) {
                            if (index == 0U) return false;
                            previous = lengths[index - 1U];
                            if (!ivt_get_bits(reader, 2U, &value)) return false;
                            repeat = 3U + (unsigned)value;
                        } else if (symbol == 17) {
                            if (!ivt_get_bits(reader, 3U, &value)) return false;
                            repeat = 3U + (unsigned)value;
                        } else {
                            if (!ivt_get_bits(reader, 7U, &value)) return false;
                            repeat = 11U + (unsigned)value;
                        }
                        if ((index + repeat) > (hlit + hdist)) return false;
                        while (repeat--) lengths[index++] = previous;
                    }
                }
                if (lengths[256] == 0U) return false; /* no end-of-block code */

                if (ivt_build(&literals, lengths, hlit) != 0) return false;
                err = ivt_build(&distances, lengths + hlit, hdist);
                /* An incomplete distance code is legal only when it holds a
                 * single code; zlib accepts exactly that case. */
                if (err < 0) return false;
                if (err > 0) {
                    unsigned used = 0U;
                    for (index = 0U; index < hdist; ++index) {
                        if (lengths[hlit + index] != 0U) used++;
                    }
                    if (used > 1U) return false;
                }
            }

            for (;;) {
                symbol = ivt_decode_symbol(reader, &literals);
                if (symbol < 0) return false;
                if (symbol < 256) {
                    if (!ivt_put(sink, (uint8_t)symbol)) return false;
                    continue;
                }
                if (symbol == 256) break;
                symbol -= 257;
                if (symbol >= 29) return false; /* codes 286/287 never valid */
                extra = ivt_length_extra[symbol];
                if (!ivt_get_bits(reader, extra, &value)) return false;
                length = (uint32_t)ivt_length_base[symbol] + value;

                symbol = ivt_decode_symbol(reader, &distances);
                if (symbol < 0) return false;
                if (symbol >= 30) return false;
                extra = ivt_dist_extra[symbol];
                if (!ivt_get_bits(reader, extra, &value)) return false;
                distance = (uint32_t)ivt_dist_base[symbol] + value;

                /* The dictionary is the preceding output, capped at the 32 KiB
                 * MSZIP hands to the next block.  A reference before that is a
                 * malformed stream: fail, never wrap or clamp. */
                max_distance = (sink->position < IVT_WINDOW) ? sink->position
                                                             : (size_t)IVT_WINDOW;
                if (((size_t)distance == 0U) || ((size_t)distance > max_distance)) {
                    return false;
                }
                source = sink->position - (size_t)distance;
                while (length--) {
                    if (!ivt_put(sink, sink->output[source++])) return false;
                }
            }
        } else {
            return false; /* BTYPE 3 is reserved */
        }

        if (final_block) break;
    }

    return true;
}

bool xx_ivt_declared_size(const uint8_t *input, size_t input_size,
                          size_t *declared)
{
    if (declared) *declared = 0U;
    if (!input || (input_size < IVT_HEADER_SIZE)) return false;

    if (!(((input[0] == 'm') && (input[1] == 's') && (input[2] == 'z') &&
           (input[3] == 'p')) ||
          ((input[0] == 'n') && (input[1] == 's') && (input[2] == 'z') &&
           (input[3] == 'p')))) {
        return false;
    }

    if (declared) {
        *declared = (size_t)((uint32_t)input[4] | ((uint32_t)input[5] << 8) |
                             ((uint32_t)input[6] << 16) |
                             ((uint32_t)input[7] << 24));
    }
    /* input[8..11] is a reserved word the reference does not inspect. */

    return true;
}

bool xx_ivt_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size, size_t *written)
{
    ivt_sink sink;
    ivt_bits reader;
    size_t declared = 0U;
    size_t position = IVT_HEADER_SIZE;
    unsigned block_uncompressed;
    unsigned block_compressed;

    if (written) *written = 0U;
    if (!input || !output) return false;
    if (!xx_ivt_declared_size(input, input_size, &declared)) return false;
    if (declared > output_size) return false;

    sink.output = output;
    sink.position = 0U;
    sink.block_end = 0U;

    while ((position + 2U) <= input_size) {
        block_uncompressed = (unsigned)input[position] |
                             ((unsigned)input[position + 1U] << 8);
        position += 2U;
        if (block_uncompressed == 0U) break;

        if ((position + 2U) > input_size) return false;
        block_compressed = (unsigned)input[position] |
                           ((unsigned)input[position + 1U] << 8);
        position += 2U;

        if ((block_compressed < 2U) || (block_uncompressed > IVT_MAX_BLOCK) ||
            ((size_t)block_compressed > (input_size - position))) {
            return false;
        }
        if (block_uncompressed > (output_size - sink.position)) return false;

        /* The "CK" signature is inside the counted block, ahead of the raw
         * DEFLATE data. */
        if ((input[position] != 'C') || (input[position + 1U] != 'K')) return false;

        reader.input = input + position + 2U;
        reader.input_size = (size_t)block_compressed - 2U;
        reader.offset = 0U;
        reader.bit_buffer = 0U;
        reader.bit_count = 0U;

        sink.block_end = sink.position + (size_t)block_uncompressed;
        if (!ivt_inflate(&reader, &sink)) return false;
        /* The block must produce exactly what its header declares: the
         * reference makes the same demand and refuses a short block. */
        if (sink.position != sink.block_end) return false;

        /* Trailing bytes inside the counted block are tolerated.  The
         * reference asserts its inflater consumed the whole payload, but that
         * is a device-read count over a 16 KiB buffered reader rather than a
         * bit-exact assertion; the block length field is what frames the
         * stream, and it has already been honoured. */
        position += (size_t)block_compressed;
    }

    if (sink.position != declared) return false;

    if (written) *written = sink.position;

    return true;
}
