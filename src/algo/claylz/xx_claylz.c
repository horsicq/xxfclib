/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Clay ("Clay" / "ClayE") member decoder.  Ported one-for-one from the
 * XArchive reference decoder (XArchive/Algos/xclaydecoder.cpp), which was
 * confirmed against all 398 reference archives.  The four fixed code tables
 * are copied verbatim from that file, and the LSB-first reader, the linear
 * table search (including its per-table look-ahead widths) and the token
 * grammar are reproduced exactly; only the output goes to a flat caller
 * buffer instead of a growing QByteArray.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/claylz/xx_claylz.h"

/* The four fixed prefix codes.  Every code is matched against the LOW bits of
 * the reader's look-ahead, so the stored value already carries the reversed
 * bit order that an LSB-first reader needs; a code of width w matches when
 * (lookahead & ((1 << w) - 1)) equals it.  These tables are read-only, so
 * they are shareable state, not mutable module state. */
static const uint16_t g_arrLiteralCode[256] = {
    1168, 4064, 2016, 3040, 992, 3552, 1504, 2528, 480, 184, 98, 3808, 1760, 34, 2784, 736,
    3296, 1248, 2272, 224, 3936, 1888, 2912, 864, 3424, 1376, 4672, 2400, 352, 3680, 1632, 2656,
    15, 592, 56, 608, 80, 3168, 912, 216, 66, 2, 88, 432, 124, 41, 60, 152,
    92, 9, 28, 108, 44, 76, 24, 12, 116, 232, 104, 1120, 144, 52, 176, 1808,
    2144, 49, 84, 17, 33, 23, 20, 168, 40, 1, 784, 304, 62, 100, 30, 46,
    36, 1296, 14, 54, 22, 68, 48, 200, 464, 208, 272, 72, 1552, 336, 96, 136,
    4000, 7, 38, 6, 58, 27, 26, 42, 10, 11, 528, 4, 19, 50, 3, 29,
    18, 400, 13, 21, 5, 25, 8, 120, 240, 112, 656, 1040, 16, 1952, 2976, 928,
    576, 7232, 3136, 5184, 1088, 6208, 2112, 4160, 64, 8064, 3968, 6016, 1920, 7040, 2944, 4992,
    896, 7552, 3456, 5504, 1408, 6528, 2432, 4480, 384, 7808, 3712, 5760, 1664, 6784, 2688, 4736,
    640, 7296, 3200, 5248, 1152, 6272, 2176, 4224, 128, 7936, 3840, 5888, 1792, 6912, 2816, 4864,
    3488, 1440, 2464, 416, 3744, 1696, 2720, 672, 3232, 1184, 2208, 160, 3872, 1824, 2848, 800,
    3360, 1312, 2336, 288, 3616, 1568, 2592, 544, 3104, 1056, 2080, 32, 4032, 1984, 3008, 960,
    3520, 1472, 2496, 448, 3776, 1728, 2752, 704, 3264, 1216, 2240, 192, 3904, 1856, 2880, 832,
    768, 3392, 7424, 3328, 5376, 1344, 1280, 6400, 2304, 2368, 4352, 256, 7680, 3584, 320, 5632,
    1536, 6656, 3648, 1600, 2624, 2560, 4608, 512, 7168, 3072, 5120, 1024, 6144, 2048, 4096, 0
};

static const uint8_t g_arrLiteralBits[256] = {
    11, 12, 12, 12, 12, 12, 12, 12, 12, 8, 7, 12, 12, 7, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 13, 12, 12, 12, 12, 12, 4, 10, 8, 12, 10, 12, 10, 8, 7, 7, 8, 9, 7, 6, 7, 8,
    7, 6, 7, 7, 7, 7, 8, 7, 7, 8, 8, 12, 11, 7, 9, 11, 12, 6, 7, 6, 6, 5, 7, 8,
    8, 6, 11, 9, 6, 7, 6, 6, 7, 11, 6, 6, 6, 7, 9, 8, 9, 9, 11, 8, 11, 9, 12, 8,
    12, 5, 6, 6, 6, 5, 6, 6, 6, 5, 11, 7, 5, 6, 5, 5, 6, 10, 5, 5, 5, 5, 8, 7,
    8, 8, 10, 11, 11, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 12, 13, 12, 13, 13, 13, 12, 13, 13, 13, 12, 13, 13, 13, 13, 12, 13,
    13, 13, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13
};

static const uint8_t g_arrLengthCode[16] = {
    5, 3, 1, 6, 10, 2, 12, 20, 4, 24, 8, 48, 16, 32, 64, 0
};

static const uint8_t g_arrLengthBits[16] = {
    3, 2, 3, 3, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 7, 7
};

static const uint16_t g_arrLengthBase[16] = {
    2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264
};

static const uint8_t g_arrLengthExtra[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8
};

static const uint8_t g_arrDistanceCode[64] = {
    3, 13, 5, 25, 9, 17, 1, 62, 30, 46, 14, 54, 22, 38, 6, 58,
    26, 42, 10, 50, 18, 34, 66, 2, 124, 60, 92, 28, 108, 44, 76, 12,
    116, 52, 84, 20, 100, 36, 68, 4, 120, 56, 88, 24, 104, 40, 72, 8,
    240, 112, 176, 48, 208, 80, 144, 16, 224, 96, 160, 32, 192, 64, 128, 0
};

static const uint8_t g_arrDistanceBits[64] = {
    2, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8
};

#define CLAY_END_OF_STREAM_LENGTH 0x207
#define CLAY_MAX_WINDOW 0x1000 /* 0x40 << 6 */
#define CLAY_MAX_OUTPUT 0x7fffffff

/* LSB-first reader.  Reading past the end fails and the caller stops; the
 * encoder pads the final byte, so the last token of a well formed stream
 * never needs those bits. */
typedef struct clay_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint64_t accumulator;
    int32_t count;
} clay_bits;

static bool clay_fill(clay_bits *bits, int32_t nbits) {
    while (bits->count < nbits) {
        if (bits->position >= bits->size) return false;
        bits->accumulator |= (uint64_t)bits->data[bits->position] << bits->count;
        ++bits->position;
        bits->count += 8;
    }
    return true;
}

static uint32_t clay_peek(const clay_bits *bits, int32_t nbits) {
    return (uint32_t)(bits->accumulator & (((uint64_t)1 << nbits) - 1));
}

static void clay_drop(clay_bits *bits, int32_t nbits) {
    bits->accumulator >>= nbits;
    bits->count -= nbits;
}

static bool clay_read(clay_bits *bits, int32_t nbits, uint32_t *value) {
    if (nbits == 0) {
        *value = 0;
        return true;
    }
    if (!clay_fill(bits, nbits)) return false;
    *value = clay_peek(bits, nbits);
    clay_drop(bits, nbits);
    return true;
}

/* Linear search over one of the fixed tables.  The tables are tiny (16, 64
 * and 256 entries), so a decode tree buys nothing measurable here.  The
 * look-ahead width is per table and is WIDER than the widest code in two of
 * them (15 for the literal table, 14 for the distance table); that is the
 * reference's own choice and it is load-bearing, because it decides whether a
 * token close to the end of the input can still be read. */
static bool clay_decode_symbol(clay_bits *bits, const uint8_t *code_bits, const uint16_t *codes16, const uint8_t *codes8, int32_t count, int32_t max_bits,
                               int32_t *symbol) {
    uint32_t lookahead;
    int32_t i;

    if (!clay_fill(bits, max_bits)) return false;
    lookahead = clay_peek(bits, max_bits);
    for (i = 0; i < count; ++i) {
        const int32_t width = (int32_t)code_bits[i];
        const uint32_t code = codes16 ? (uint32_t)codes16[i] : (uint32_t)codes8[i];
        if ((lookahead & ((1U << width) - 1U)) == code) {
            clay_drop(bits, width);
            *symbol = i;
            return true;
        }
    }
    return false;
}

/* The token loop.  `window` is the caller-owned ring, already zeroed. */
static bool clay_decode_body(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, uint8_t *window, uint32_t window_mask,
                             uint8_t window_exponent, bool huffman_literals, size_t *produced_out) {
    clay_bits bits;
    uint32_t write_position = 0;
    size_t produced = 0;

    *produced_out = 0;

    bits.data = input + 2;
    bits.size = input_size - 2;
    bits.position = 0;
    bits.accumulator = 0;
    bits.count = 0;

    while (produced < output_size) {
        uint32_t flag = 0;
        int32_t length_class = 0;
        uint32_t length_extra = 0;
        int32_t length;
        int32_t distance_class = 0;
        uint32_t distance_low = 0;
        uint32_t distance;
        uint32_t source;
        int32_t i;

        if (!clay_read(&bits, 1, &flag)) return false;

        if (flag == 0) {
            uint32_t literal = 0;
            if (huffman_literals) {
                int32_t symbol = 0;
                if (!clay_decode_symbol(&bits, g_arrLiteralBits, g_arrLiteralCode, 0, 256, 15, &symbol)) return false;
                literal = (uint32_t)symbol;
            } else {
                if (!clay_read(&bits, 8, &literal)) return false;
            }
            output[produced] = (uint8_t)literal;
            produced++;
            window[write_position] = (uint8_t)literal;
            write_position = (write_position + 1) & window_mask;
            continue;
        }

        if (!clay_decode_symbol(&bits, g_arrLengthBits, 0, g_arrLengthCode, 16, 7, &length_class)) return false;
        if (!clay_read(&bits, (int32_t)g_arrLengthExtra[length_class], &length_extra)) return false;
        length = (int32_t)g_arrLengthBase[length_class] + (int32_t)length_extra;
        /* 0x207 is the largest value the table can express and is the end of
         * stream marker, not a length.  The reference stops there and then
         * requires the declared plaintext length to have been reached, so a
         * marker arriving early is a failed decode. */
        if (length == CLAY_END_OF_STREAM_LENGTH) return false;

        if (!clay_decode_symbol(&bits, g_arrDistanceBits, 0, g_arrDistanceCode, 64, 14, &distance_class)) return false;

        if (length == 2) {
            /* Deliberate: a length of exactly 2 uses a 2-bit low half and
             * scales the class by 4 regardless of the window exponent. */
            if (!clay_read(&bits, 2, &distance_low)) return false;
            distance = ((uint32_t)distance_class * 4U) + distance_low;
        } else {
            if (!clay_read(&bits, (int32_t)window_exponent, &distance_low)) return false;
            distance = ((uint32_t)distance_class << window_exponent) + distance_low;
        }

        /* The source is masked into the ring, so a distance larger than what
         * has been written so far reads never-written zeros rather than
         * failing; the reference tolerates this and the ring is the only
         * addressable history, so there is no out-of-bounds access. */
        source = (write_position - distance - 1U) & window_mask;
        /* The reference lets the final match run past the declared length and
         * then rejects the member, so an overrun is a failed decode here. */
        if ((size_t)length > (output_size - produced)) return false;
        for (i = 0; i < length; ++i) {
            const uint8_t byte = window[source];
            output[produced] = byte;
            produced++;
            window[write_position] = byte;
            write_position = (write_position + 1) & window_mask;
            source = (source + 1) & window_mask;
        }
    }

    *produced_out = produced;

    return produced == output_size;
}

bool xx_claylz_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written) {
    uint8_t *window;
    uint8_t literal_mode;
    uint8_t window_exponent;
    int32_t window_size;
    size_t produced = 0;
    bool decoded;

    if (written) *written = 0;
    if (output_size > (size_t)CLAY_MAX_OUTPUT) return false;
    if (!input) return false;
    if (input_size < 2) return false;
    if ((output_size > 0) && !output) return false;

    literal_mode = input[0];
    window_exponent = input[1];
    if (literal_mode > 1) return false;
    if ((window_exponent < 4) || (window_exponent > 6)) return false;

    window_size = 0x40 << window_exponent;

    /* The ring is at most 4 KiB; it lives on the heap rather than the stack so
     * the module keeps a small, predictable frame. */
    window = (uint8_t *)xx_mem_alloc((size_t)window_size);
    if (!window) return false;
    xx_rt_memset(window, 0, (size_t)window_size);

    decoded = clay_decode_body(input, input_size, output, output_size, window, (uint32_t)(window_size - 1), window_exponent, (literal_mode != 0), &produced);

    xx_mem_free(window);

    if (!decoded) return false;
    if (written) *written = produced;

    return true;
}
