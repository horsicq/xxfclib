/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ported from the LZFSE reference implementation published by Apple Inc.
 * under a three-clause BSD licence (github.com/lzfse/lzfse): the block
 * layout, the FSE table construction, the backwards bit reader and the LZVN
 * opcode map all follow that source.  The decoder here is one-shot rather
 * than resumable - the whole destination is supplied up front, so none of
 * the reference's "destination buffer is full" bookkeeping is needed - and
 * every wide load and store of the original has been replaced by a bounded
 * byte copy, because the reference's fast paths deliberately read and write
 * past the ranges they are copying and rely on slack the caller cannot see.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzfse/xx_lzfse.h"

#include "xxfclib/memory/xx_memory.h"

/* ------------------------------------------------------------------------ */
/* Format constants                                                          */
/* ------------------------------------------------------------------------ */

#define XX_LZFSE_L_SYMBOLS 20
#define XX_LZFSE_M_SYMBOLS 20
#define XX_LZFSE_D_SYMBOLS 64
#define XX_LZFSE_LITERAL_SYMBOLS 256
#define XX_LZFSE_L_STATES 64
#define XX_LZFSE_M_STATES 64
#define XX_LZFSE_D_STATES 256
#define XX_LZFSE_LITERAL_STATES 1024

#define XX_LZFSE_FREQ_SYMBOLS                                                 \
    (XX_LZFSE_L_SYMBOLS + XX_LZFSE_M_SYMBOLS + XX_LZFSE_D_SYMBOLS +           \
     XX_LZFSE_LITERAL_SYMBOLS)

#define XX_LZFSE_MATCHES_PER_BLOCK 10000U
#define XX_LZFSE_LITERALS_PER_BLOCK (4U * XX_LZFSE_MATCHES_PER_BLOCK)

/* The fixed part of a v2 header: magic, n_raw_bytes and three packed u64. */
#define XX_LZFSE_V2_FIXED_SIZE 32U
/* Every frequency stored at its widest spelling, which is what the reference
 * sizes its own freq[] array for. */
#define XX_LZFSE_V2_MAX_SIZE (XX_LZFSE_V2_FIXED_SIZE + 2U * XX_LZFSE_FREQ_SYMBOLS)

/* A v1 header is memcpy'd straight out of the stream by the reference, so its
 * on-disk layout is the C layout of lzfse_compressed_block_header_v1: scalars
 * to offset 50, then the four frequency tables packed end to end. */
#define XX_LZFSE_V1_FREQ_OFFSET 50U
#define XX_LZFSE_V1_SIZE 772U

/* An uncompressed block header, and an lzvn block header. */
#define XX_LZFSE_UNCOMPRESSED_HEADER_SIZE 8U
#define XX_LZFSE_LZVN_HEADER_SIZE 12U

/* L, M and D are each an FSE-coded base plus a raw remainder.  The tables are
 * reproduced from the reference; the encoder and decoder must agree on them
 * exactly or the streams decode to plausible garbage. */
static const uint8_t xx_lzfse_l_extra_bits[XX_LZFSE_L_SYMBOLS] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 5, 8};
static const int32_t xx_lzfse_l_base_value[XX_LZFSE_L_SYMBOLS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 28, 60};
static const uint8_t xx_lzfse_m_extra_bits[XX_LZFSE_M_SYMBOLS] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 5, 8, 11};
static const int32_t xx_lzfse_m_base_value[XX_LZFSE_M_SYMBOLS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 24, 56, 312};
static const uint8_t xx_lzfse_d_extra_bits[XX_LZFSE_D_SYMBOLS] = {
    0,  0,  0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,
    4,  4,  4,  4,  5,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,
    8,  8,  8,  8,  9,  9,  9,  9,  10, 10, 10, 10, 11, 11, 11, 11,
    12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15};
static const int32_t xx_lzfse_d_base_value[XX_LZFSE_D_SYMBOLS] = {
    0,      1,      2,      3,      4,      6,      8,      10,
    12,     16,     20,     24,     28,     36,     44,     52,
    60,     76,     92,     108,    124,    156,    188,    220,
    252,    316,    380,    444,    508,    636,    764,    892,
    1020,   1276,   1532,   1788,   2044,   2556,   3068,   3580,
    4092,   5116,   6140,   7164,   8188,   10236,  12284,  14332,
    16380,  20476,  24572,  28668,  32764,  40956,  49148,  57340,
    65532,  81916,  98300,  114684, 131068, 163836, 196604, 229372};

/* ------------------------------------------------------------------------ */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------ */

/* Everything in an LZFSE stream is little endian, and nothing in it is
 * guaranteed to be aligned, so every multi-byte field is assembled a byte at
 * a time rather than loaded through a cast. */
static uint32_t xx_lzfse_load4(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t xx_lzfse_load_n(const uint8_t *data, size_t size) {
    uint64_t value = 0U;
    size_t index;
    for (index = 0U; index < size; ++index) {
        value |= (uint64_t)data[index] << (8U * index);
    }
    return value;
}

static int xx_lzfse_clz32(uint32_t value) {
    int count = 0;
    if (value == 0U) return 32;
    while ((value & UINT32_C(0x80000000)) == 0U) {
        value <<= 1;
        ++count;
    }
    return count;
}

/* Keep only the low nbits of value; nbits is always in [0, 64]. */
static uint64_t xx_lzfse_mask_lsb64(uint64_t value, int nbits) {
    if (nbits <= 0) return 0U;
    if (nbits >= 64) return value;
    return value & ((UINT64_C(1) << nbits) - 1U);
}

/* ------------------------------------------------------------------------ */
/* The backwards bit reader                                                  */
/* ------------------------------------------------------------------------ */

/* An FSE payload is written forwards but read backwards, so the reader holds
 * a cursor that walks down towards buf_start and an accumulator kept between
 * 56 and 63 bits full.  The reference keeps it strictly under 64 so that a
 * pull never has to special-case an empty shift. */
typedef struct xx_lzfse_in_stream_s {
    uint64_t accum;
    int accum_nbits;
} xx_lzfse_in_stream;

/* Position the cursor and prime the accumulator.  nbits is the encoder's
 * final bit count, carried in the block header, and is in [-7, 0]. */
static bool xx_lzfse_in_init(xx_lzfse_in_stream *stream, int nbits,
                             const uint8_t *buffer, size_t *cursor,
                             size_t buffer_start, size_t buffer_size) {
    size_t position = *cursor;
    if (nbits != 0) {
        if (position < buffer_start + 8U) return false;
        position -= 8U;
        stream->accum = xx_lzfse_load_n(buffer + position, 8U);
        stream->accum_nbits = nbits + 64;
    } else {
        if (position < buffer_start + 7U) return false;
        position -= 7U;
        stream->accum = xx_lzfse_load_n(buffer + position, 7U);
        stream->accum_nbits = 56;
    }
    (void)buffer_size;
    if (stream->accum_nbits < 56 || stream->accum_nbits >= 64 ||
        (stream->accum >> stream->accum_nbits) != 0U) {
        /* The encoder zeroes the bits above accum_nbits, so anything there
         * means the payload is not the one the header describes. */
        return false;
    }
    *cursor = position;
    return true;
}

/* Refill to between 56 and 63 bits, stepping the cursor down by whole bytes. */
static bool xx_lzfse_in_flush(xx_lzfse_in_stream *stream, const uint8_t *buffer,
                              size_t *cursor, size_t buffer_start,
                              size_t buffer_size) {
    int nbits = (63 - stream->accum_nbits) & -8;
    size_t position;
    uint64_t incoming;
    if (nbits <= 0) return true;
    if ((size_t)(nbits >> 3) > *cursor || *cursor - (size_t)(nbits >> 3) < buffer_start) {
        return false;
    }
    position = *cursor - (size_t)(nbits >> 3);
    /* The reference reads eight bytes here unconditionally; the cursor can
     * only ever have moved down from the payload end, so eight bytes are
     * always in range, but the bound is checked rather than assumed. */
    if (position > buffer_size || buffer_size - position < 8U) return false;
    incoming = xx_lzfse_load_n(buffer + position, 8U);
    stream->accum = (stream->accum << nbits) | xx_lzfse_mask_lsb64(incoming, nbits);
    stream->accum_nbits += nbits;
    *cursor = position;
    return true;
}

/* Take the top n bits off the accumulator. */
static bool xx_lzfse_in_pull(xx_lzfse_in_stream *stream, int nbits,
                             uint64_t *out_value) {
    if (nbits < 0 || nbits > stream->accum_nbits) return false;
    stream->accum_nbits -= nbits;
    *out_value = stream->accum >> stream->accum_nbits;
    stream->accum = xx_lzfse_mask_lsb64(stream->accum, stream->accum_nbits);
    return true;
}

/* ------------------------------------------------------------------------ */
/* FSE tables                                                                */
/* ------------------------------------------------------------------------ */

/* One decoder state for the literal stream.  The reference packs these three
 * fields into an int32_t and unpacks them with shifts, which silently assumes
 * a little-endian struct layout; they are kept as named fields here. */
typedef struct xx_lzfse_decoder_entry_s {
    int8_t k;        /**< Bits of state to read back from the stream. */
    uint8_t symbol;  /**< The byte this state emits. */
    int16_t delta;   /**< Added to the bits read, giving the next state. */
} xx_lzfse_decoder_entry;

/* One decoder state for L, M or D, where the symbol stands for a base value
 * plus a raw remainder read from the same stream. */
typedef struct xx_lzfse_value_decoder_entry_s {
    uint8_t total_bits;  /**< State bits plus value bits. */
    uint8_t value_bits;  /**< Just the raw remainder. */
    int16_t delta;
    int32_t vbase;
} xx_lzfse_value_decoder_entry;

/* Build the state table for one stream.  The frequencies are normalised so
 * that they sum to nstates; a shortfall leaves the tail of the table zeroed,
 * which decodes to symbol 0 and state 0 and so can never leave the table. */
static bool xx_lzfse_init_decoder_table(int nstates, int nsymbols,
                                        const uint16_t *freq,
                                        xx_lzfse_decoder_entry *table) {
    int n_clz = xx_lzfse_clz32((uint32_t)nstates);
    int sum_of_freq = 0;
    int index;
    int slot = 0;
    for (index = 0; index < nsymbols; ++index) {
        int f = (int)freq[index];
        int k;
        int j0;
        int j;
        if (f == 0) continue;
        sum_of_freq += f;
        if (sum_of_freq > nstates) return false;
        k = xx_lzfse_clz32((uint32_t)f) - n_clz;
        if (k < 0) return false;
        j0 = ((2 * nstates) >> k) - f;
        for (j = 0; j < f; ++j) {
            xx_lzfse_decoder_entry entry;
            entry.symbol = (uint8_t)index;
            if (j < j0) {
                entry.k = (int8_t)k;
                entry.delta = (int16_t)(((f + j) << k) - nstates);
            } else {
                /* k is zero only when one symbol owns every state, and then
                 * j0 == f so this branch is unreachable; refuse rather than
                 * shift by a negative count if that ever stops holding. */
                if (k < 1) return false;
                entry.k = (int8_t)(k - 1);
                entry.delta = (int16_t)((j - j0) << (k - 1));
            }
            table[slot++] = entry;
        }
    }
    return true;
}

static bool xx_lzfse_init_value_decoder_table(
    int nstates, int nsymbols, const uint16_t *freq, const uint8_t *value_bits,
    const int32_t *value_base, xx_lzfse_value_decoder_entry *table) {
    int n_clz = xx_lzfse_clz32((uint32_t)nstates);
    int sum_of_freq = 0;
    int index;
    int slot = 0;
    for (index = 0; index < nsymbols; ++index) {
        int f = (int)freq[index];
        int k;
        int j0;
        int j;
        if (f == 0) continue;
        sum_of_freq += f;
        if (sum_of_freq > nstates) return false;
        k = xx_lzfse_clz32((uint32_t)f) - n_clz;
        if (k < 0) return false;
        j0 = ((2 * nstates) >> k) - f;
        for (j = 0; j < f; ++j) {
            xx_lzfse_value_decoder_entry entry;
            entry.value_bits = value_bits[index];
            entry.vbase = value_base[index];
            if (j < j0) {
                entry.total_bits = (uint8_t)(k + (int)entry.value_bits);
                entry.delta = (int16_t)(((f + j) << k) - nstates);
            } else {
                if (k < 1) return false;
                entry.total_bits = (uint8_t)((k - 1) + (int)entry.value_bits);
                entry.delta = (int16_t)((j - j0) << (k - 1));
            }
            table[slot++] = entry;
        }
    }
    return true;
}

static bool xx_lzfse_check_freq(const uint16_t *freq, int count, int nstates) {
    int sum = 0;
    int index;
    for (index = 0; index < count; ++index) sum += (int)freq[index];
    return sum <= nstates;
}

/* ------------------------------------------------------------------------ */
/* Block header decoding                                                     */
/* ------------------------------------------------------------------------ */

/* Everything a compressed block needs, whether it arrived as v1 or v2. */
typedef struct xx_lzfse_block_header_s {
    uint32_t n_raw_bytes;
    uint32_t n_literals;
    uint32_t n_matches;
    uint32_t n_literal_payload_bytes;
    uint32_t n_lmd_payload_bytes;
    int literal_bits;
    int lmd_bits;
    uint16_t literal_state[4];
    uint16_t l_state;
    uint16_t m_state;
    uint16_t d_state;
    uint16_t freq[XX_LZFSE_FREQ_SYMBOLS];
} xx_lzfse_block_header;

static uint32_t xx_lzfse_get_field(uint64_t value, int offset, int nbits) {
    if (nbits >= 32) return (uint32_t)(value >> offset);
    return (uint32_t)((value >> offset) & ((UINT64_C(1) << nbits) - 1U));
}

/* Decode one frequency from the low bits of accum, reporting how many bits it
 * consumed.  The short values are a five-bit prefix code; the two long forms
 * carry their remainder in the bits above it. */
static int xx_lzfse_decode_freq_value(uint32_t bits, int *out_nbits) {
    static const int8_t nbits_table[32] = {
        2, 3, 2, 5, 2, 3, 2, 8,  2, 3, 2, 5, 2, 3, 2, 14,
        2, 3, 2, 5, 2, 3, 2, 8,  2, 3, 2, 5, 2, 3, 2, 14};
    static const int8_t value_table[32] = {
        0, 2, 1, 4, 0, 3, 1, -1, 0, 2, 1, 5, 0, 3, 1, -1,
        0, 2, 1, 6, 0, 3, 1, -1, 0, 2, 1, 7, 0, 3, 1, -1};
    uint32_t low = bits & 31U;
    int n = nbits_table[low];
    *out_nbits = n;
    if (n == 8) return 8 + (int)((bits >> 4) & 0xfU);
    if (n == 14) return 24 + (int)((bits >> 4) & 0x3ffU);
    return value_table[low];
}

/* Unpack a v2 header, whose scalars are bit-packed into three 64-bit words
 * and whose frequency tables are stored in the prefix code above. */
static bool xx_lzfse_decode_header_v2(const uint8_t *data, size_t size,
                                      xx_lzfse_block_header *header,
                                      size_t *out_header_size) {
    uint64_t v0;
    uint64_t v1;
    uint64_t v2;
    uint32_t header_size;
    size_t cursor;
    size_t end;
    uint32_t accum = 0U;
    int accum_nbits = 0;
    int index;
    if (size < XX_LZFSE_V2_FIXED_SIZE) return false;
    v0 = xx_lzfse_load_n(data + 8U, 8U);
    v1 = xx_lzfse_load_n(data + 16U, 8U);
    v2 = xx_lzfse_load_n(data + 24U, 8U);
    header_size = xx_lzfse_get_field(v2, 0, 32);
    if (header_size < XX_LZFSE_V2_FIXED_SIZE ||
        header_size > XX_LZFSE_V2_MAX_SIZE || (size_t)header_size > size) {
        return false;
    }
    xx_rt_memset(header, 0, sizeof(*header));
    header->n_raw_bytes = xx_lzfse_load4(data + 4U);
    header->n_literals = xx_lzfse_get_field(v0, 0, 20);
    header->n_literal_payload_bytes = xx_lzfse_get_field(v0, 20, 20);
    header->n_matches = xx_lzfse_get_field(v0, 40, 20);
    header->literal_bits = (int)xx_lzfse_get_field(v0, 60, 3) - 7;
    header->literal_state[0] = (uint16_t)xx_lzfse_get_field(v1, 0, 10);
    header->literal_state[1] = (uint16_t)xx_lzfse_get_field(v1, 10, 10);
    header->literal_state[2] = (uint16_t)xx_lzfse_get_field(v1, 20, 10);
    header->literal_state[3] = (uint16_t)xx_lzfse_get_field(v1, 30, 10);
    header->n_lmd_payload_bytes = xx_lzfse_get_field(v1, 40, 20);
    header->lmd_bits = (int)xx_lzfse_get_field(v1, 60, 3) - 7;
    header->l_state = (uint16_t)xx_lzfse_get_field(v2, 32, 10);
    header->m_state = (uint16_t)xx_lzfse_get_field(v2, 42, 10);
    header->d_state = (uint16_t)xx_lzfse_get_field(v2, 52, 10);

    cursor = XX_LZFSE_V2_FIXED_SIZE;
    end = (size_t)header_size;
    *out_header_size = end;
    /* A header that stops at the fixed part carries no frequencies at all,
     * which is legal and means every table is empty. */
    if (cursor == end) return true;
    for (index = 0; index < XX_LZFSE_FREQ_SYMBOLS; ++index) {
        int nbits = 0;
        int value;
        while (cursor < end && accum_nbits + 8 <= 32) {
            accum |= (uint32_t)data[cursor] << accum_nbits;
            accum_nbits += 8;
            ++cursor;
        }
        value = xx_lzfse_decode_freq_value(accum, &nbits);
        if (nbits > accum_nbits || value < 0 || value > 0xFFFF) return false;
        header->freq[index] = (uint16_t)value;
        accum >>= nbits;
        accum_nbits -= nbits;
    }
    /* The frequencies must end exactly at the end of the header, with only
     * padding bits left over. */
    return accum_nbits < 8 && cursor == end;
}

/* Unpack a v1 header, whose fields sit in the stream at the offsets the
 * reference's struct gives them. */
static bool xx_lzfse_decode_header_v1(const uint8_t *data, size_t size,
                                      xx_lzfse_block_header *header,
                                      size_t *out_header_size) {
    int index;
    if (size < XX_LZFSE_V1_SIZE) return false;
    xx_rt_memset(header, 0, sizeof(*header));
    header->n_raw_bytes = xx_lzfse_load4(data + 4U);
    header->n_literals = xx_lzfse_load4(data + 12U);
    header->n_matches = xx_lzfse_load4(data + 16U);
    header->n_literal_payload_bytes = xx_lzfse_load4(data + 20U);
    header->n_lmd_payload_bytes = xx_lzfse_load4(data + 24U);
    header->literal_bits = (int)(int32_t)xx_lzfse_load4(data + 28U);
    header->literal_state[0] = (uint16_t)xx_lzfse_load_n(data + 32U, 2U);
    header->literal_state[1] = (uint16_t)xx_lzfse_load_n(data + 34U, 2U);
    header->literal_state[2] = (uint16_t)xx_lzfse_load_n(data + 36U, 2U);
    header->literal_state[3] = (uint16_t)xx_lzfse_load_n(data + 38U, 2U);
    header->lmd_bits = (int)(int32_t)xx_lzfse_load4(data + 40U);
    header->l_state = (uint16_t)xx_lzfse_load_n(data + 44U, 2U);
    header->m_state = (uint16_t)xx_lzfse_load_n(data + 46U, 2U);
    header->d_state = (uint16_t)xx_lzfse_load_n(data + 48U, 2U);
    for (index = 0; index < XX_LZFSE_FREQ_SYMBOLS; ++index) {
        header->freq[index] = (uint16_t)xx_lzfse_load_n(
            data + XX_LZFSE_V1_FREQ_OFFSET + (size_t)index * 2U, 2U);
    }
    *out_header_size = XX_LZFSE_V1_SIZE;
    return true;
}

static bool xx_lzfse_check_header(const xx_lzfse_block_header *header) {
    if (header->n_literals > XX_LZFSE_LITERALS_PER_BLOCK) return false;
    if (header->n_matches > XX_LZFSE_MATCHES_PER_BLOCK) return false;
    if (header->literal_state[0] >= XX_LZFSE_LITERAL_STATES ||
        header->literal_state[1] >= XX_LZFSE_LITERAL_STATES ||
        header->literal_state[2] >= XX_LZFSE_LITERAL_STATES ||
        header->literal_state[3] >= XX_LZFSE_LITERAL_STATES) {
        return false;
    }
    if (header->l_state >= XX_LZFSE_L_STATES ||
        header->m_state >= XX_LZFSE_M_STATES ||
        header->d_state >= XX_LZFSE_D_STATES) {
        return false;
    }
    /* literal_bits and lmd_bits index straight into the bit reader's
     * initialisation, which only accepts [-7, 0]. */
    if (header->literal_bits > 0 || header->literal_bits < -7) return false;
    if (header->lmd_bits > 0 || header->lmd_bits < -7) return false;
    return xx_lzfse_check_freq(header->freq, XX_LZFSE_L_SYMBOLS,
                               XX_LZFSE_L_STATES) &&
           xx_lzfse_check_freq(header->freq + XX_LZFSE_L_SYMBOLS,
                               XX_LZFSE_M_SYMBOLS, XX_LZFSE_M_STATES) &&
           xx_lzfse_check_freq(
               header->freq + XX_LZFSE_L_SYMBOLS + XX_LZFSE_M_SYMBOLS,
               XX_LZFSE_D_SYMBOLS, XX_LZFSE_D_STATES) &&
           xx_lzfse_check_freq(header->freq + XX_LZFSE_L_SYMBOLS +
                                   XX_LZFSE_M_SYMBOLS + XX_LZFSE_D_SYMBOLS,
                               XX_LZFSE_LITERAL_SYMBOLS,
                               XX_LZFSE_LITERAL_STATES);
}

/* ------------------------------------------------------------------------ */
/* LZVN                                                                      */
/* ------------------------------------------------------------------------ */

/* The opcode classes of the LZVN byte code, as the reference's 256-entry
 * dispatch table assigns them. */
typedef enum xx_lzvn_opcode_e {
    XX_LZVN_OPC_UNDEFINED = 0,
    XX_LZVN_OPC_SMALL_D,   /**< LLMMMDDD DDDDDDDD literal...  */
    XX_LZVN_OPC_MEDIUM_D,  /**< 101LLMMM DDDDDDMM DDDDDDDD literal... */
    XX_LZVN_OPC_LARGE_D,   /**< LLMMM111 DDDDDDDD DDDDDDDD literal... */
    XX_LZVN_OPC_PREVIOUS_D,/**< LLMMM110, reusing the last distance */
    XX_LZVN_OPC_SMALL_M,   /**< 1111MMMM */
    XX_LZVN_OPC_LARGE_M,   /**< 11110000 MMMMMMMM */
    XX_LZVN_OPC_SMALL_L,   /**< 1110LLLL literal... */
    XX_LZVN_OPC_LARGE_L,   /**< 11100000 LLLLLLLL literal... */
    XX_LZVN_OPC_NOP,
    XX_LZVN_OPC_EOS
} xx_lzvn_opcode;

/* Classify one opcode byte.  The distance-bearing opcodes occupy three
 * disjoint ranges of the byte, split by their low three bits; everything
 * outside those ranges is either a single-purpose opcode or undefined. */
static xx_lzvn_opcode xx_lzvn_classify(uint8_t opcode) {
    unsigned high = (unsigned)opcode >> 3;
    unsigned low = (unsigned)opcode & 7U;
    if (opcode >= 0xF1U) return XX_LZVN_OPC_SMALL_M;
    if (opcode == 0xF0U) return XX_LZVN_OPC_LARGE_M;
    if (opcode >= 0xE1U && opcode <= 0xEFU) return XX_LZVN_OPC_SMALL_L;
    if (opcode == 0xE0U) return XX_LZVN_OPC_LARGE_L;
    if (opcode >= 0xA0U && opcode <= 0xBFU) return XX_LZVN_OPC_MEDIUM_D;
    if (opcode == 0x06U) return XX_LZVN_OPC_EOS;
    if (opcode == 0x0EU || opcode == 0x16U) return XX_LZVN_OPC_NOP;
    /* The ranges that carry a distance: 0x00-0x6F, 0x80-0x9F, 0xC0-0xCF. */
    if (high <= 13U || (high >= 16U && high <= 19U) ||
        (high >= 24U && high <= 25U)) {
        if (low < 6U) return XX_LZVN_OPC_SMALL_D;
        if (low == 7U) return XX_LZVN_OPC_LARGE_D;
        /* low == 6: the "previous distance" form, but only in the upper two
         * ranges - in the lowest one those bytes are eos, nop or undefined,
         * which the tests above have already claimed. */
        if (high >= 8U) return XX_LZVN_OPC_PREVIOUS_D;
    }
    return XX_LZVN_OPC_UNDEFINED;
}

/* Decode one LZVN payload.
 *
 * Output goes into destination[*position ...], and matches may reach back as
 * far as destination[0]: inside an LZFSE stream an LZVN block's matches are
 * validated against the whole output, not against the block.  The payload
 * must end with the eight-byte end-of-stream opcode. */
static bool xx_lzvn_decode(const uint8_t *source, size_t source_size,
                           uint8_t *destination, size_t limit,
                           size_t *position, size_t *out_consumed) {
    size_t cursor = 0U;
    size_t pos = *position;
    uint32_t distance = 0U;
    if (!source || !destination || pos > limit) return false;
    for (;;) {
        uint8_t opcode;
        size_t opcode_size;
        size_t literal = 0U;
        size_t match = 0U;
        bool has_literal = false;
        bool has_match = false;
        if (cursor >= source_size) return false; /* truncated */
        opcode = source[cursor];
        switch (xx_lzvn_classify(opcode)) {
            case XX_LZVN_OPC_SMALL_D:
                opcode_size = 2U;
                literal = ((size_t)opcode >> 6) & 3U;
                match = (((size_t)opcode >> 3) & 7U) + 3U;
                /* Every opcode must be followed by at least one more byte,
                 * because a well-formed stream always ends with eos. */
                if (source_size - cursor <= opcode_size + literal) return false;
                distance = (((uint32_t)opcode & 7U) << 8) |
                           (uint32_t)source[cursor + 1U];
                has_literal = true;
                has_match = true;
                break;
            case XX_LZVN_OPC_MEDIUM_D: {
                uint32_t pair;
                opcode_size = 3U;
                literal = ((size_t)opcode >> 3) & 3U;
                if (source_size - cursor <= opcode_size + literal) return false;
                pair = (uint32_t)source[cursor + 1U] |
                       ((uint32_t)source[cursor + 2U] << 8);
                match = (size_t)(((((uint32_t)opcode & 7U) << 2) |
                                  (pair & 3U)) + 3U);
                distance = (pair >> 2) & 0x3FFFU;
                has_literal = true;
                has_match = true;
                break;
            }
            case XX_LZVN_OPC_LARGE_D:
                opcode_size = 3U;
                literal = ((size_t)opcode >> 6) & 3U;
                match = (((size_t)opcode >> 3) & 7U) + 3U;
                if (source_size - cursor <= opcode_size + literal) return false;
                distance = (uint32_t)source[cursor + 1U] |
                           ((uint32_t)source[cursor + 2U] << 8);
                has_literal = true;
                has_match = true;
                break;
            case XX_LZVN_OPC_PREVIOUS_D:
                opcode_size = 1U;
                literal = ((size_t)opcode >> 6) & 3U;
                match = (((size_t)opcode >> 3) & 7U) + 3U;
                if (source_size - cursor <= opcode_size + literal) return false;
                has_literal = true;
                has_match = true;
                break;
            case XX_LZVN_OPC_SMALL_M:
                opcode_size = 1U;
                if (source_size - cursor <= opcode_size) return false;
                match = (size_t)opcode & 15U;
                has_match = true;
                break;
            case XX_LZVN_OPC_LARGE_M:
                opcode_size = 2U;
                if (source_size - cursor <= opcode_size) return false;
                match = (size_t)source[cursor + 1U] + 16U;
                has_match = true;
                break;
            case XX_LZVN_OPC_SMALL_L:
                opcode_size = 1U;
                literal = (size_t)opcode & 15U;
                if (source_size - cursor <= opcode_size + literal) return false;
                has_literal = true;
                break;
            case XX_LZVN_OPC_LARGE_L:
                opcode_size = 2U;
                if (source_size - cursor <= opcode_size) return false;
                literal = (size_t)source[cursor + 1U] + 16U;
                if (source_size - cursor <= opcode_size + literal) return false;
                has_literal = true;
                break;
            case XX_LZVN_OPC_NOP:
                opcode_size = 1U;
                if (source_size - cursor <= opcode_size) return false;
                cursor += opcode_size;
                continue;
            case XX_LZVN_OPC_EOS:
                if (source_size - cursor < 8U) return false;
                cursor += 8U;
                *position = pos;
                if (out_consumed) *out_consumed = cursor;
                return true;
            default: return false;
        }
        cursor += opcode_size;
        if (has_literal) {
            if (literal > limit - pos) return false;
            if (literal != 0U) {
                xx_rt_memcpy(destination + pos, source + cursor, literal);
                pos += literal;
                cursor += literal;
            }
        }
        if (has_match) {
            size_t index;
            /* A match may not reach before the output, and a zero distance
             * would stand still; the "previous distance" opcode inherits
             * whatever the last real one left, which is zero at the start of
             * a block and so is refused. */
            if (distance == 0U || (uint64_t)distance > (uint64_t)pos) {
                return false;
            }
            if (match > limit - pos) return false;
            /* Byte at a time: the distance may be smaller than the length,
             * in which case the copy must see the bytes it just wrote. */
            for (index = 0U; index < match; ++index) {
                destination[pos + index] = destination[pos + index - distance];
            }
            pos += match;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Compressed block decoding                                                 */
/* ------------------------------------------------------------------------ */

/* The per-block tables and the literal buffer, together about 48 KiB, which
 * is why they are heap allocated rather than put on the caller's stack. */
typedef struct xx_lzfse_work_s {
    xx_lzfse_decoder_entry literal_decoder[XX_LZFSE_LITERAL_STATES];
    xx_lzfse_value_decoder_entry l_decoder[XX_LZFSE_L_STATES];
    xx_lzfse_value_decoder_entry m_decoder[XX_LZFSE_M_STATES];
    xx_lzfse_value_decoder_entry d_decoder[XX_LZFSE_D_STATES];
    uint8_t literals[XX_LZFSE_LITERALS_PER_BLOCK + 64U];
    xx_lzfse_block_header header;
} xx_lzfse_work;

static bool xx_lzfse_decode_literals(xx_lzfse_work *work, const uint8_t *source,
                                     size_t source_size, size_t payload_end) {
    const xx_lzfse_block_header *header = &work->header;
    xx_lzfse_in_stream stream;
    size_t cursor = payload_end;
    uint16_t state[4];
    uint32_t index;
    state[0] = header->literal_state[0];
    state[1] = header->literal_state[1];
    state[2] = header->literal_state[2];
    state[3] = header->literal_state[3];
    /* The reference lets the literal reader walk back to the start of the
     * whole input, not just the start of this payload, so the same bound is
     * used here: a valid stream never needs it, but rejecting it would
     * reject streams the reference accepts. */
    if (!xx_lzfse_in_init(&stream, header->literal_bits, source, &cursor, 0U,
                          source_size)) {
        return false;
    }
    for (index = 0U; index < header->n_literals; index += 4U) {
        int slot;
        if (!xx_lzfse_in_flush(&stream, source, &cursor, 0U, source_size)) {
            return false;
        }
        for (slot = 0; slot < 4; ++slot) {
            xx_lzfse_decoder_entry entry = work->literal_decoder[state[slot]];
            uint64_t bits;
            if (!xx_lzfse_in_pull(&stream, entry.k, &bits)) return false;
            state[slot] = (uint16_t)((int32_t)entry.delta + (int32_t)bits);
            if (state[slot] >= XX_LZFSE_LITERAL_STATES) return false;
            work->literals[index + (uint32_t)slot] = entry.symbol;
        }
    }
    return true;
}

static bool xx_lzfse_decode_lmd(xx_lzfse_work *work, const uint8_t *source,
                                size_t lmd_start, size_t lmd_end,
                                uint8_t *destination, size_t destination_size,
                                size_t *position) {
    const xx_lzfse_block_header *header = &work->header;
    xx_lzfse_in_stream stream;
    size_t cursor = lmd_end;
    size_t literal_cursor = 0U;
    size_t pos = *position;
    uint16_t l_state = header->l_state;
    uint16_t m_state = header->m_state;
    uint16_t d_state = header->d_state;
    int32_t distance = -1;
    uint32_t symbols = header->n_matches;
    if (!xx_lzfse_in_init(&stream, header->lmd_bits, source, &cursor, lmd_start,
                          lmd_end)) {
        return false;
    }
    while (symbols-- > 0U) {
        xx_lzfse_value_decoder_entry entry;
        uint64_t bits;
        int32_t run_length;
        int32_t match_length;
        int32_t new_distance;
        size_t index;

        if (!xx_lzfse_in_flush(&stream, source, &cursor, lmd_start, lmd_end)) {
            return false;
        }
        entry = work->l_decoder[l_state];
        if (!xx_lzfse_in_pull(&stream, entry.total_bits, &bits)) return false;
        l_state = (uint16_t)((int32_t)entry.delta +
                             (int32_t)(bits >> entry.value_bits));
        if (l_state >= XX_LZFSE_L_STATES) return false;
        run_length = entry.vbase +
                     (int32_t)xx_lzfse_mask_lsb64(bits, entry.value_bits);

        entry = work->m_decoder[m_state];
        if (!xx_lzfse_in_pull(&stream, entry.total_bits, &bits)) return false;
        m_state = (uint16_t)((int32_t)entry.delta +
                             (int32_t)(bits >> entry.value_bits));
        if (m_state >= XX_LZFSE_M_STATES) return false;
        match_length = entry.vbase +
                       (int32_t)xx_lzfse_mask_lsb64(bits, entry.value_bits);

        entry = work->d_decoder[d_state];
        if (!xx_lzfse_in_pull(&stream, entry.total_bits, &bits)) return false;
        d_state = (uint16_t)((int32_t)entry.delta +
                             (int32_t)(bits >> entry.value_bits));
        if (d_state >= XX_LZFSE_D_STATES) return false;
        new_distance = entry.vbase +
                       (int32_t)xx_lzfse_mask_lsb64(bits, entry.value_bits);
        /* A zero distance repeats the previous one.  The first triplet of a
         * block has no previous one, so the sentinel keeps it from passing. */
        if (new_distance != 0) distance = new_distance;

        if (run_length < 0 || match_length < 0) return false;
        if ((size_t)run_length > sizeof(work->literals) - literal_cursor) {
            return false;
        }
        if ((size_t)run_length > destination_size - pos) return false;
        if (run_length != 0) {
            xx_rt_memcpy(destination + pos, work->literals + literal_cursor,
                         (size_t)run_length);
            pos += (size_t)run_length;
            literal_cursor += (size_t)run_length;
        }
        /* The distance is checked against the position AFTER the literal
         * run, which is where the match is about to be written. */
        if (distance <= 0 || (uint64_t)distance > (uint64_t)pos) return false;
        if ((size_t)match_length > destination_size - pos) return false;
        for (index = 0U; index < (size_t)match_length; ++index) {
            destination[pos + index] =
                destination[pos + index - (size_t)distance];
        }
        pos += (size_t)match_length;
    }
    *position = pos;
    return true;
}

static bool xx_lzfse_decode_compressed(xx_lzfse_work *work, uint32_t magic,
                                       const uint8_t *source,
                                       size_t source_size, size_t *cursor,
                                       uint8_t *destination,
                                       size_t destination_size,
                                       size_t *position) {
    xx_lzfse_block_header *header = &work->header;
    size_t header_size = 0U;
    size_t block_start = *cursor;
    size_t literal_end;
    size_t lmd_start;
    size_t lmd_end;
    size_t produced;
    size_t before = *position;
    if (magic == XX_LZFSE_MAGIC_COMPRESSEDV2) {
        if (!xx_lzfse_decode_header_v2(source + block_start,
                                       source_size - block_start, header,
                                       &header_size)) {
            return false;
        }
    } else if (!xx_lzfse_decode_header_v1(source + block_start,
                                          source_size - block_start, header,
                                          &header_size)) {
        return false;
    }
    /* The whole encoded block must be present before decoding starts. */
    if (header_size > source_size - block_start) return false;
    literal_end = block_start + header_size;
    if ((size_t)header->n_literal_payload_bytes > source_size - literal_end) {
        return false;
    }
    lmd_start = literal_end + header->n_literal_payload_bytes;
    if ((size_t)header->n_lmd_payload_bytes > source_size - lmd_start) {
        return false;
    }
    lmd_end = lmd_start + header->n_lmd_payload_bytes;
    if (!xx_lzfse_check_header(header)) return false;

    xx_rt_memset(work->literal_decoder, 0, sizeof(work->literal_decoder));
    xx_rt_memset(work->l_decoder, 0, sizeof(work->l_decoder));
    xx_rt_memset(work->m_decoder, 0, sizeof(work->m_decoder));
    xx_rt_memset(work->d_decoder, 0, sizeof(work->d_decoder));
    xx_rt_memset(work->literals, 0, sizeof(work->literals));
    if (!xx_lzfse_init_value_decoder_table(
            XX_LZFSE_L_STATES, XX_LZFSE_L_SYMBOLS, header->freq,
            xx_lzfse_l_extra_bits, xx_lzfse_l_base_value, work->l_decoder) ||
        !xx_lzfse_init_value_decoder_table(
            XX_LZFSE_M_STATES, XX_LZFSE_M_SYMBOLS,
            header->freq + XX_LZFSE_L_SYMBOLS, xx_lzfse_m_extra_bits,
            xx_lzfse_m_base_value, work->m_decoder) ||
        !xx_lzfse_init_value_decoder_table(
            XX_LZFSE_D_STATES, XX_LZFSE_D_SYMBOLS,
            header->freq + XX_LZFSE_L_SYMBOLS + XX_LZFSE_M_SYMBOLS,
            xx_lzfse_d_extra_bits, xx_lzfse_d_base_value, work->d_decoder) ||
        !xx_lzfse_init_decoder_table(
            XX_LZFSE_LITERAL_STATES, XX_LZFSE_LITERAL_SYMBOLS,
            header->freq + XX_LZFSE_L_SYMBOLS + XX_LZFSE_M_SYMBOLS +
                XX_LZFSE_D_SYMBOLS,
            work->literal_decoder)) {
        return false;
    }
    if (!xx_lzfse_decode_literals(work, source, source_size, lmd_start)) {
        return false;
    }
    if (!xx_lzfse_decode_lmd(work, source, lmd_start, lmd_end, destination,
                             destination_size, position)) {
        return false;
    }
    produced = *position - before;
    /* The header states exactly how many bytes the block expands to; a
     * mismatch means the L/M/D stream and the header disagree. */
    if (produced != (size_t)header->n_raw_bytes) return false;
    *cursor = lmd_end;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Public interface                                                          */
/* ------------------------------------------------------------------------ */

bool xx_lzfse_is_available(void) { return true; }

bool xx_lzfse_header_is_valid(const void *source, size_t source_size) {
    uint32_t magic;
    if (!source || source_size < 4U) return false;
    magic = xx_lzfse_load4((const uint8_t *)source);
    return magic == XX_LZFSE_MAGIC_ENDOFSTREAM ||
           magic == XX_LZFSE_MAGIC_UNCOMPRESSED ||
           magic == XX_LZFSE_MAGIC_COMPRESSEDV1 ||
           magic == XX_LZFSE_MAGIC_COMPRESSEDV2 ||
           magic == XX_LZFSE_MAGIC_COMPRESSEDLZVN;
}

bool xx_lzvn_decompress_memory(const void *source, size_t source_size,
                               void *destination, size_t destination_size,
                               size_t *out_written, size_t *out_consumed) {
    size_t position = 0U;
    size_t consumed = 0U;
    if (!source || !destination) return false;
    if (!xx_lzvn_decode((const uint8_t *)source, source_size,
                        (uint8_t *)destination, destination_size, &position,
                        &consumed)) {
        return false;
    }
    if (out_written) *out_written = position;
    if (out_consumed) *out_consumed = consumed;
    return true;
}

bool xx_lzfse_decompress_memory(const void *source, size_t source_size,
                                void *destination, size_t destination_size,
                                size_t *out_written) {
    const uint8_t *input = (const uint8_t *)source;
    uint8_t *output = (uint8_t *)destination;
    xx_lzfse_work *work = NULL;
    size_t cursor = 0U;
    size_t position = 0U;
    bool result = false;
    if (!input || (!output && destination_size != 0U)) return false;
    for (;;) {
        uint32_t magic;
        if (source_size - cursor < 4U) goto done;
        magic = xx_lzfse_load4(input + cursor);
        if (magic == XX_LZFSE_MAGIC_ENDOFSTREAM) {
            cursor += 4U;
            result = true;
            goto done;
        }
        if (magic == XX_LZFSE_MAGIC_UNCOMPRESSED) {
            uint32_t raw;
            if (source_size - cursor < XX_LZFSE_UNCOMPRESSED_HEADER_SIZE) {
                goto done;
            }
            raw = xx_lzfse_load4(input + cursor + 4U);
            cursor += XX_LZFSE_UNCOMPRESSED_HEADER_SIZE;
            if ((size_t)raw > source_size - cursor) goto done;
            if ((size_t)raw > destination_size - position) goto done;
            if (raw != 0U) {
                xx_rt_memcpy(output + position, input + cursor, (size_t)raw);
            }
            cursor += (size_t)raw;
            position += (size_t)raw;
            continue;
        }
        if (magic == XX_LZFSE_MAGIC_COMPRESSEDLZVN) {
            uint32_t raw;
            uint32_t payload;
            size_t consumed = 0U;
            size_t before = position;
            if (source_size - cursor < XX_LZFSE_LZVN_HEADER_SIZE) goto done;
            raw = xx_lzfse_load4(input + cursor + 4U);
            payload = xx_lzfse_load4(input + cursor + 8U);
            cursor += XX_LZFSE_LZVN_HEADER_SIZE;
            if ((size_t)payload > source_size - cursor) goto done;
            if ((size_t)raw > destination_size - position) goto done;
            if (!xx_lzvn_decode(input + cursor, (size_t)payload, output,
                                position + (size_t)raw, &position,
                                &consumed)) {
                goto done;
            }
            /* The header's two counts are the contract for the block; a
             * payload that stops early, or expands to the wrong size, is a
             * different stream than the one described. */
            if (consumed != (size_t)payload ||
                position - before != (size_t)raw) {
                goto done;
            }
            cursor += (size_t)payload;
            continue;
        }
        if (magic == XX_LZFSE_MAGIC_COMPRESSEDV1 ||
            magic == XX_LZFSE_MAGIC_COMPRESSEDV2) {
            if (!work) {
                work = (xx_lzfse_work *)xx_mem_alloc(sizeof(*work));
                if (!work) goto done;
            }
            if (!xx_lzfse_decode_compressed(work, magic, input, source_size,
                                            &cursor, output, destination_size,
                                            &position)) {
                goto done;
            }
            continue;
        }
        goto done; /* not a block magic this decoder knows */
    }
done:
    if (work) xx_mem_free(work);
    if (result && out_written) *out_written = position;
    return result;
}
