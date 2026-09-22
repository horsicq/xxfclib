/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Port of XArchive/Algos/xpcsecuredecoder.cpp plus the key search that
 * XArchive/archives/xpcsecure.cpp performs before calling it.  Keeping the two
 * together is what lets this module take a whole file and need no key from the
 * caller: every key PCSECURE uses is either one of four constants or is stored
 * in the file's own password verifier.
 */
#include "xxfclib/algo/pcsecure/xx_pcsecure.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

/* ------------------------------------------------------------- tables --- */
/* Standard DES.  Bit 1 is the most significant bit of the value permuted,
 * which is also the most significant bit of its first byte. */

static const uint8_t PCS_PC1[56] = {
    57, 49, 41, 33, 25, 17, 9,  1,  58, 50, 42, 34, 26, 18,
    10, 2,  59, 51, 43, 35, 27, 19, 11, 3,  60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15, 7,  62, 54, 46, 38, 30, 22,
    14, 6,  61, 53, 45, 37, 29, 21, 13, 5,  28, 20, 12, 4};

static const uint8_t PCS_PC2[48] = {
    14, 17, 11, 24, 1,  5,  3,  28, 15, 6,  21, 10,
    23, 19, 12, 4,  26, 8,  16, 7,  27, 20, 13, 2,
    41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32};

static const uint8_t PCS_IP[64] = {
    58, 50, 42, 34, 26, 18, 10, 2,  60, 52, 44, 36, 28,
    20, 12, 4,  62, 54, 46, 38, 30, 22, 14, 6,  64, 56,
    48, 40, 32, 24, 16, 8,  57, 49, 41, 33, 25, 17, 9,
    1,  59, 51, 43, 35, 27, 19, 11, 3,  61, 53, 45, 37,
    29, 21, 13, 5,  63, 55, 47, 39, 31, 23, 15, 7};

static const uint8_t PCS_FP[64] = {
    40, 8,  48, 16, 56, 24, 64, 32, 39, 7,  47, 15, 55,
    23, 63, 31, 38, 6,  46, 14, 54, 22, 62, 30, 37, 5,
    45, 13, 53, 21, 61, 29, 36, 4,  44, 12, 52, 20, 60,
    28, 35, 3,  43, 11, 51, 19, 59, 27, 34, 2,  42, 10,
    50, 18, 58, 26, 33, 1,  41, 9,  49, 17, 57, 25};

static const uint8_t PCS_E[48] = {
    32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,
    8,  9,  10, 11, 12, 13, 12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};

static const uint8_t PCS_P[32] = {
    16, 7,  20, 21, 29, 12, 28, 17, 1,  15, 23,
    26, 5,  18, 31, 10, 2,  8,  24, 14, 32, 27,
    3,  9,  19, 13, 30, 6,  22, 11, 4,  25};

static const uint8_t PCS_SHIFTS[16] = {1, 1, 2, 2, 2, 2, 2, 2,
                                       1, 2, 2, 2, 2, 2, 2, 1};

static const uint8_t PCS_SBOX[8][64] = {
    {14, 4,  13, 1, 2,  15, 11, 8,  3,  10, 6,  12, 5,  9,  0, 7,
     0,  15, 7,  4, 14, 2,  13, 1,  10, 6,  12, 11, 9,  5,  3, 8,
     4,  1,  14, 8, 13, 6,  2,  11, 15, 12, 9,  7,  3,  10, 5, 0,
     15, 12, 8,  2, 4,  9,  1,  7,  5,  11, 3,  14, 10, 0,  6, 13},
    {15, 1,  8,  14, 6,  11, 3,  4,  9,  7, 2,  13, 12, 0, 5,  10,
     3,  13, 4,  7,  15, 2,  8,  14, 12, 0, 1,  10, 6,  9, 11, 5,
     0,  14, 7,  11, 10, 4,  13, 1,  5,  8, 12, 6,  9,  3, 2,  15,
     13, 8,  10, 1,  3,  15, 4,  2,  11, 6, 7,  12, 0,  5, 14, 9},
    {10, 0,  9,  14, 6, 3,  15, 5,  1,  13, 12, 7,  11, 4,  2,  8,
     13, 7,  0,  9,  3, 4,  6,  10, 2,  8,  5,  14, 12, 11, 15, 1,
     13, 6,  4,  9,  8, 15, 3,  0,  11, 1,  2,  12, 5,  10, 14, 7,
     1,  10, 13, 0,  6, 9,  8,  7,  4,  15, 14, 3,  11, 5,  2,  12},
    {7,  13, 14, 3, 0,  6,  9,  10, 1,  2, 8, 5,  11, 12, 4,  15,
     13, 8,  11, 5, 6,  15, 0,  3,  4,  7, 2, 12, 1,  10, 14, 9,
     10, 6,  9,  0, 12, 11, 7,  13, 15, 1, 3, 14, 5,  2,  8,  4,
     3,  15, 0,  6, 10, 1,  13, 8,  9,  4, 5, 11, 12, 7,  2,  14},
    {2,  12, 4,  1,  7,  10, 11, 6,  8,  5,  3,  15, 13, 0, 14, 9,
     14, 11, 2,  12, 4,  7,  13, 1,  5,  0,  15, 10, 3,  9, 8,  6,
     4,  2,  1,  11, 10, 13, 7,  8,  15, 9,  12, 5,  6,  3, 0,  14,
     11, 8,  12, 7,  1,  14, 2,  13, 6,  15, 0,  9,  10, 4, 5,  3},
    {12, 1,  10, 15, 9, 2,  6,  8,  0,  13, 3,  4,  14, 7,  5,  11,
     10, 15, 4,  2,  7, 12, 9,  5,  6,  1,  13, 14, 0,  11, 3,  8,
     9,  14, 15, 5,  2, 8,  12, 3,  7,  0,  4,  10, 1,  13, 11, 6,
     4,  3,  2,  12, 9, 5,  15, 10, 11, 14, 1,  7,  6,  0,  8,  13},
    {4,  11, 2,  14, 15, 0, 8,  13, 3,  12, 9,  7,  5,  10, 6, 1,
     13, 0,  11, 7,  4,  9, 1,  10, 14, 3,  5,  12, 2,  15, 8, 6,
     1,  4,  11, 13, 12, 3, 7,  14, 10, 15, 6,  8,  0,  5,  9, 2,
     6,  11, 13, 8,  1,  4, 10, 7,  9,  5,  0,  15, 14, 2,  3, 12},
    {13, 2,  8,  4,  6,  15, 11, 1,  10, 9,  3,  14, 5,  0,  12, 7,
     1,  15, 13, 8,  10, 3,  7,  4,  12, 5,  6,  11, 0,  14, 9,  2,
     7,  11, 4,  1,  9,  12, 14, 2,  0,  6,  10, 13, 15, 3,  5,  8,
     2,  1,  14, 7,  4,  10, 8,  13, 15, 12, 9,  0,  3,  5,  6,  11}};

/* The four built-in product keys, stored in MEMORY ORDER - the byte reverse of
 * the quad words the reference holds (0x0489cf09a84cb420, 0xf03606ff259275dd,
 * 0xa9e9721989bca97c, 0x4f279ef1fad9666e).  DES numbers key bit 1 as the most
 * significant bit of the FIRST key byte, so the schedule has to see the bytes
 * this way round.  Transcribing the quad words verbatim makes every header
 * probe fail - do not "fix" the ordering. */
static const uint8_t PCS_BUILTIN_KEYS[4][8] = {
    {0x20, 0xb4, 0x4c, 0xa8, 0x09, 0xcf, 0x89, 0x04},
    {0xdd, 0x75, 0x92, 0x25, 0xff, 0x06, 0x36, 0xf0},
    {0x7c, 0xa9, 0xbc, 0x89, 0x19, 0x72, 0xe9, 0xa9},
    {0x6e, 0x66, 0xd9, 0xfa, 0xf1, 0x9e, 0x27, 0x4f}};

/* The fixed key that unwraps a user-password verifier into the file key. */
static const uint8_t PCS_VERIFIER_KEY[8] = {0xc1, 0xb0, 0xdc, 0x21,
                                            0xb0, 0x96, 0x4e, 0x7f};

/* ---------------------------------------------------------------- DES --- */

static uint64_t pcs_permute(uint64_t value, int32_t input_bits,
                            const uint8_t *table, int32_t output_bits)
{
    uint64_t result = 0U;
    int32_t i;

    for (i = 0; i < output_bits; ++i) {
        const int32_t from = (int32_t)table[i];
        const uint64_t bit = (value >> (input_bits - from)) & (uint64_t)1U;
        result |= bit << (output_bits - 1 - i);
    }

    return result;
}

static uint32_t pcs_rotate28(uint32_t value, int32_t count)
{
    return ((value << count) | (value >> (28 - count))) & 0x0fffffffU;
}

static void pcs_subkeys(const uint8_t key[8], uint64_t *subkeys)
{
    uint64_t packed = 0U;
    uint64_t permuted;
    uint32_t c, d;
    int32_t i;

    for (i = 0; i < 8; ++i) packed = (packed << 8) | (uint64_t)key[i];

    permuted = pcs_permute(packed, 64, PCS_PC1, 56);
    c = (uint32_t)((permuted >> 28) & 0x0fffffffU);
    d = (uint32_t)(permuted & 0x0fffffffU);
    for (i = 0; i < 16; ++i) {
        uint64_t cd;
        c = pcs_rotate28(c, (int32_t)PCS_SHIFTS[i]);
        d = pcs_rotate28(d, (int32_t)PCS_SHIFTS[i]);
        cd = ((uint64_t)c << 28) | (uint64_t)d;
        subkeys[i] = pcs_permute(cd, 56, PCS_PC2, 48);
    }
}

static uint32_t pcs_feistel(uint32_t right, uint64_t subkey)
{
    const uint64_t expanded =
        pcs_permute((uint64_t)right, 32, PCS_E, 48) ^ subkey;
    uint32_t merged = 0U;
    int32_t i;

    for (i = 0; i < 8; ++i) {
        const uint32_t six = (uint32_t)((expanded >> (42 - i * 6)) & 0x3fU);
        const uint32_t row = ((six >> 4) & 0x02U) | (six & 0x01U);
        const uint32_t column = (six >> 1) & 0x0fU;
        merged |= (uint32_t)PCS_SBOX[i][row * 16U + column] << (28 - i * 4);
    }

    return (uint32_t)pcs_permute((uint64_t)merged, 32, PCS_P, 32);
}

/* One decrypting block pass.
 *
 * DELIBERATE, DO NOT "FIX": when `rounds` is below 3 the initial and final
 * permutations are SKIPPED.  The original replaces them with a 32-bit endian
 * swap, which is the identity once the halves are treated as big-endian words,
 * so skipping is exactly equivalent.  Running standard IP/FP at 2 rounds turns
 * most of the corpus into garbage.  Note also that the halves are swapped into
 * the result unconditionally - at 0 rounds that swap is the entire transform. */
static void pcs_decrypt_block(const uint8_t *in, uint8_t *out,
                              const uint64_t *subkeys, int32_t rounds)
{
    uint64_t block = 0U;
    uint64_t result;
    uint32_t left, right;
    int32_t i;

    for (i = 0; i < 8; ++i) block = (block << 8) | (uint64_t)in[i];
    if (rounds >= 3) block = pcs_permute(block, 64, PCS_IP, 64);

    left = (uint32_t)(block >> 32);
    right = (uint32_t)(block & 0xffffffffU);
    for (i = rounds - 1; i >= 0; --i) {
        const uint32_t next = left ^ pcs_feistel(right, subkeys[i]);
        left = right;
        right = next;
    }

    result = ((uint64_t)right << 32) | (uint64_t)left;
    if (rounds >= 3) result = pcs_permute(result, 64, PCS_FP, 64);
    for (i = 0; i < 8; ++i) {
        out[i] = (uint8_t)((result >> (56 - i * 8)) & 0xffU);
    }
}

/* ------------------------------------------------------------- endian --- */

static uint16_t pcs_read_u16le(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static uint32_t pcs_read_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void pcs_write_u16le(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xffU);
    p[1] = (uint8_t)((value >> 8) & 0xffU);
}

static void pcs_write_u32le(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xffU);
    p[1] = (uint8_t)((value >> 8) & 0xffU);
    p[2] = (uint8_t)((value >> 16) & 0xffU);
    p[3] = (uint8_t)((value >> 24) & 0xffU);
}

static uint32_t pcs_swap32(uint32_t value)
{
    return ((value & 0x000000ffU) << 24) | ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) | ((value & 0xff000000U) >> 24);
}

/* The header mixes big-endian fields into a little-endian record; these are the
 * exact offsets the reference fixes up, no more. */
static void pcs_fix_header_byte_order(uint8_t *header)
{
    static const uint8_t dword_offsets[7] = {4, 8, 0x18, 0x1c, 0x20, 0x24, 0x38};
    static const uint8_t word_offsets[2] = {0x0c, 0x16};
    int32_t i;

    for (i = 0; i < 7; ++i) {
        const uint32_t value = pcs_swap32(pcs_read_u32le(header + dword_offsets[i]));
        pcs_write_u32le(header + dword_offsets[i], value);
    }
    for (i = 0; i < 2; ++i) {
        const uint16_t value = pcs_read_u16le(header + word_offsets[i]);
        pcs_write_u16le(header + word_offsets[i],
                        (uint16_t)(((value & 0x00ffU) << 8) | (value >> 8)));
    }
}

/* ---------------------------------------------------------------- LZW --- */

#define PCS_LZW_MAX_BITS 14
#define PCS_LZW_MAX_CODES (1 << PCS_LZW_MAX_BITS)
#define PCS_LZW_FIRST_CODE 0x101

typedef struct pcs_lzw_tables {
    uint16_t prefix[PCS_LZW_MAX_CODES];
    uint8_t suffix[PCS_LZW_MAX_CODES];
    uint8_t stack[PCS_LZW_MAX_CODES + 1];
} pcs_lzw_tables;

/* LSB-first LZW, 9..14 bits, CLEAR = 0x100 and NO end code, first assignable
 * code 0x101.  Transcribed from pcsLzw(); the quirks kept verbatim are marked
 * inline.  Returns true only when exactly `limit` bytes came out. */
static bool pcs_lzw(const uint8_t *data, size_t size, size_t limit,
                    uint8_t *output, size_t *written)
{
    pcs_lzw_tables *t;
    size_t pos = 0U;
    size_t out_pos = 0U;
    uint32_t buffer = 0U;
    int32_t bits = 0;
    int32_t width = 9;
    int32_t max_code = 0x1ff;
    int32_t next_free = PCS_LZW_FIRST_CODE;
    int32_t prev_code = 0;
    int32_t first_char = 0;
    size_t left = limit;
    bool first = true;
    int32_t i;

    t = (pcs_lzw_tables *)xx_mem_alloc(sizeof(pcs_lzw_tables));
    if (!t) return false;
    xx_rt_memset(t->prefix, 0, sizeof(t->prefix));
    xx_rt_memset(t->suffix, 0, sizeof(t->suffix));
    xx_rt_memset(t->stack, 0, sizeof(t->stack));
    for (i = 0; i < 256; ++i) t->suffix[i] = (uint8_t)i;

    while (left > 0U) {
        int32_t code;
        int32_t stack_size;
        int32_t current;
        bool overflow;

        /* Width grows BEFORE the fetch, once the table has outgrown the
         * current width.  At width 14 max_code becomes 16384, not 16383, which
         * is what stops the width from ever growing again - deliberate. */
        if (max_code < next_free) {
            ++width;
            max_code = (width == PCS_LZW_MAX_BITS) ? PCS_LZW_MAX_CODES
                                                   : ((1 << width) - 1);
        }
        while (bits < width) {
            if (pos >= size) break;
            buffer |= (uint32_t)data[pos] << bits;
            ++pos;
            bits += 8;
        }
        if (bits < width) break;
        code = (int32_t)(buffer & ((1U << width) - 1U));
        buffer >>= width;
        bits -= width;

        if (first) {
            /* The opening code is emitted verbatim as a literal and seeds the
             * previous code.  It is NOT masked to 8 bits before becoming
             * prev_code, only the emitted byte is - kept as in the reference. */
            first = false;
            prev_code = code;
            first_char = code & 0xff;
            output[out_pos++] = (uint8_t)code;
            --left;
            continue;
        }

        if (code == 0x100) {
            int32_t seed;
            width = 9;
            max_code = 0x1ff;
            next_free = PCS_LZW_FIRST_CODE;
            /* The reference repeats the widening test here; after the reset it
             * can never fire (0x1ff < 0x101 is false).  Kept for fidelity. */
            if (max_code < next_free) {
                ++width;
                max_code = (width == PCS_LZW_MAX_BITS) ? PCS_LZW_MAX_CODES
                                                       : ((1 << width) - 1);
            }
            while (bits < width) {
                if (pos >= size) break;
                buffer |= (uint32_t)data[pos] << bits;
                ++pos;
                bits += 8;
            }
            if (bits < width) break;
            seed = (int32_t)(buffer & ((1U << width) - 1U));
            buffer >>= width;
            bits -= width;
            if (seed > 0xff) break; /* 0x100 or a non-literal ends the stream */
            prev_code = seed;
            first_char = seed;
            output[out_pos++] = (uint8_t)seed;
            --left;
            continue;
        }

        stack_size = 0;
        current = code;
        if (code >= next_free) {
            t->stack[stack_size++] = (uint8_t)first_char;
            current = prev_code;
        }
        overflow = false;
        while (current > 0xff) {
            if (stack_size >= PCS_LZW_MAX_CODES) {
                overflow = true;
                break;
            }
            /* current is a 14-bit code, so both table reads are in range. */
            t->stack[stack_size++] = t->suffix[current];
            current = (int32_t)t->prefix[current];
        }
        if (overflow) break;
        t->stack[stack_size++] = (uint8_t)(current & 0xff);
        first_char = current & 0xff;

        while ((stack_size > 0) && (left > 0U)) {
            --stack_size;
            output[out_pos++] = t->stack[stack_size];
            --left;
        }
        /* A match that overshoots the declared length is truncated and the
         * stream ends there - the reference does the same. */
        if (left == 0U) break;

        if (next_free < PCS_LZW_MAX_CODES) {
            t->prefix[next_free] = (uint16_t)prev_code;
            t->suffix[next_free] = (uint8_t)first_char;
            ++next_free;
        }
        prev_code = code;
    }

    xx_mem_free(t);
    *written = out_pos;

    return (out_pos == limit);
}

/* -------------------------------------------------------------- header --- */

static bool pcs_is_known_signature(uint32_t signature)
{
    return (signature == 0x35544350U) || /* "PCT5" */
           (signature == 0x36544350U) || /* "PCT6" */
           (signature == 0x37544350U) || /* "PCT7" */
           (signature == 0x536f6641U);   /* "AfoS" */
}

/* Decrypts the header's encrypted region with `key` at `rounds` rounds and
 * reports whether the "SeaHawks" verifier came out.  `plain` receives the
 * 68-byte header with the byte-order fixups applied. */
static bool pcs_try_header(const uint8_t *header, const uint8_t key[8],
                           int32_t rounds, uint8_t *plain)
{
    uint64_t subkeys[16];
    uint32_t high, low;
    uint16_t payload_rounds, swapped;
    int32_t i;

    if ((rounds < 0) || (rounds > XX_PCSECURE_MAX_ROUNDS)) return false;

    pcs_subkeys(key, subkeys);

    for (i = 0; i < XX_PCSECURE_HEADER_SIZE; ++i) plain[i] = header[i];
    /* Bytes 4..59 are the encrypted region: seven blocks.  0..3 is the ASCII
     * signature and 60..67 the plaintext password verifier. */
    for (i = 0; i < 7; ++i) {
        pcs_decrypt_block(header + 4 + i * 8, plain + 4 + i * 8, subkeys, rounds);
    }

    high = pcs_read_u32le(plain + 0x28) ^ pcs_read_u32le(plain + 0x30);
    low = pcs_read_u32le(plain + 0x2c) ^ pcs_read_u32le(plain + 0x34);
    /* "SeaHawks", split across two XOR pairs. */
    if ((high != 0x48616553U) || (low != 0x736b7761U)) return false;

    payload_rounds = pcs_read_u16le(plain + 0x0c);
    swapped = (uint16_t)(((payload_rounds & 0x00ffU) << 8) |
                         (payload_rounds >> 8));
    /* The payload round count must be 0..16 before the header is accepted;
     * this is what stops a lucky verifier collision. */
    if (swapped > (uint16_t)XX_PCSECURE_MAX_ROUNDS) return false;

    pcs_fix_header_byte_order(plain);

    return true;
}

bool xx_pcsecure_parse_header(const uint8_t *input, size_t input_size,
                              xx_pcsecure_info *info)
{
    uint8_t candidates[5][8];
    uint8_t plain[XX_PCSECURE_HEADER_SIZE];
    uint8_t verifier_plain[8];
    uint64_t verifier_subkeys[16];
    uint32_t signature;
    uint32_t uncompressed, compressed;
    size_t data_size;
    int32_t candidate_count = 0;
    int32_t found = -1;
    int32_t i, j;
    bool verifier_nonzero = false;

    if (!input || !info) return false;
    /* Strictly more than 68 bytes: a header with no payload is not a file. */
    if (input_size <= (size_t)XX_PCSECURE_HEADER_SIZE) return false;

    signature = pcs_read_u32le(input);
    if (!pcs_is_known_signature(signature)) return false;

    data_size = input_size - (size_t)XX_PCSECURE_HEADER_SIZE;

    for (i = 0; i < 8; ++i) {
        if (input[60 + i] != 0) verifier_nonzero = true;
    }
    if (verifier_nonzero) {
        /* The verifier is one DES block encrypted with a fixed key at 16
         * rounds, and its plaintext IS the file key.  No corpus sample carries
         * a non-zero verifier, so this branch mirrors the reference but is not
         * corpus-verified. */
        pcs_subkeys(PCS_VERIFIER_KEY, verifier_subkeys);
        pcs_decrypt_block(input + 60, verifier_plain, verifier_subkeys, 16);
        for (i = 0; i < 8; ++i) candidates[candidate_count][i] = verifier_plain[i];
        ++candidate_count;
    }
    for (j = 0; j < 4; ++j) {
        for (i = 0; i < 8; ++i) candidates[candidate_count][i] = PCS_BUILTIN_KEYS[j][i];
        ++candidate_count;
    }

    for (i = 0; (i < candidate_count) && (found < 0); ++i) {
        if (pcs_try_header(input, candidates[i], 16, plain)) {
            found = i;
        } else if ((signature == 0x37544350U) &&
                   pcs_try_header(input, candidates[i], 3, plain)) {
            /* PCT7 additionally allows a 3-round header. */
            found = i;
        }
    }
    if (found < 0) return false;

    uncompressed = pcs_read_u32le(plain + 0x18);
    compressed = pcs_read_u32le(plain + 0x20);
    if ((uncompressed == 0U) ||
        ((uint64_t)uncompressed > (uint64_t)XX_PCSECURE_MAX_OUTPUT)) {
        return false;
    }
    if ((compressed == 0U) || ((uint64_t)compressed > (uint64_t)data_size)) {
        return false;
    }

    for (i = 0; i < 8; ++i) info->key[i] = candidates[found][i];
    info->signature = signature;
    info->flags = pcs_read_u32le(plain + 8);
    info->rounds = (int32_t)pcs_read_u16le(plain + 0x0c);
    info->uncompressed_size = (uint64_t)uncompressed;
    info->compressed_size = (uint64_t)compressed;
    info->dos_time = pcs_read_u32le(plain + 0x38);
    for (i = 0; i < 4; ++i) info->name_extension[i] = plain[0x12 + i];
    info->user_password = verifier_nonzero;

    return true;
}

/* -------------------------------------------------------------- decode --- */

bool xx_pcsecure_decode_payload(const uint8_t *payload, size_t payload_size,
                                const uint8_t key[8], int32_t rounds,
                                uint8_t flags, uint64_t compressed_size,
                                uint64_t uncompressed_size, uint8_t *output,
                                size_t output_size, size_t *written)
{
    uint64_t subkeys[16];
    uint8_t *plain;
    size_t blocks;
    size_t i;
    size_t produced = 0U;
    size_t input_bytes;
    bool result;

    if (written) *written = 0U;
    if (!key) return false;
    if (!payload && (payload_size != 0U)) return false;
    if ((rounds < 0) || (rounds > XX_PCSECURE_MAX_ROUNDS)) return false;
    if (uncompressed_size > (uint64_t)XX_PCSECURE_MAX_OUTPUT) return false;
    if (uncompressed_size == 0U) return true; /* reference: empty, success */
    if ((uint64_t)output_size < uncompressed_size) return false;
    if (!output) return false;

    pcs_subkeys(key, subkeys);

    /* ECB over whole 8-byte blocks; a short tail is carried through untouched,
     * exactly as the reference's block stream does. */
    plain = (uint8_t *)xx_mem_alloc(payload_size ? payload_size : 1U);
    if (!plain) return false;
    if (payload_size != 0U) xx_rt_memcpy(plain, payload, payload_size);
    blocks = payload_size / (size_t)XX_PCSECURE_BLOCK_SIZE;
    for (i = 0U; i < blocks; ++i) {
        pcs_decrypt_block(payload + i * XX_PCSECURE_BLOCK_SIZE,
                          plain + i * XX_PCSECURE_BLOCK_SIZE, subkeys, rounds);
    }

    if (flags & 0x01U) {
        input_bytes = (size_t)compressed_size;
        if ((compressed_size == 0U) || (compressed_size > (uint64_t)payload_size)) {
            input_bytes = payload_size;
        }
        result = pcs_lzw(plain, input_bytes, (size_t)uncompressed_size, output,
                         &produced);
    } else {
        if ((uint64_t)payload_size < uncompressed_size) {
            xx_mem_free(plain);
            return false;
        }
        xx_rt_memcpy(output, plain, (size_t)uncompressed_size);
        produced = (size_t)uncompressed_size;
        result = true;
    }

    xx_mem_free(plain);
    if (!result) return false;
    if (written) *written = produced;

    return true;
}

bool xx_pcsecure_decode_memory(const uint8_t *input, size_t input_size,
                               uint8_t *output, size_t output_size,
                               size_t *written)
{
    xx_pcsecure_info info;

    if (written) *written = 0U;
    if (!xx_pcsecure_parse_header(input, input_size, &info)) return false;

    return xx_pcsecure_decode_payload(input + XX_PCSECURE_HEADER_SIZE,
                                      input_size - (size_t)XX_PCSECURE_HEADER_SIZE,
                                      info.key, info.rounds,
                                      (uint8_t)(info.flags & 0xffU),
                                      info.compressed_size,
                                      info.uncompressed_size, output,
                                      output_size, written);
}

bool xx_pcsecure_scan_memory(const uint8_t *input, size_t input_size,
                             size_t max_output, size_t *consumed,
                             size_t *produced)
{
    xx_pcsecure_info info;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!xx_pcsecure_parse_header(input, input_size, &info)) return false;
    if (info.uncompressed_size > (uint64_t)max_output) return false;

    if (consumed) *consumed = input_size;
    if (produced) *produced = (size_t)info.uncompressed_size;

    return true;
}
