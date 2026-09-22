/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Stac Electronics SAF member codec.  Ported one-for-one from the XArchive
 * reference decoder (XArchive/Algos/xsafdecoder.cpp).  The bit reader, the
 * 9-bit token dispatch, the escaping length code and the 2 KiB ring are
 * reproduced exactly; the only structural difference is that this port writes
 * decoded bytes straight to the caller's buffer instead of appending the ring
 * to a QByteArray every 2048 bytes and flushing the tail at the end.  That is
 * output-equivalent: the reference appends every byte it stores into the ring
 * exactly once and in the order it stored it (full-window flushes plus one
 * final partial flush), so the concatenation is the same sequence of bytes.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/saf/xx_saf.h"

#define SAF_WINDOW 0x800
#define SAF_WINDOW_MASK (SAF_WINDOW - 1)

typedef struct saf_bits_s {
    const uint8_t *data;
    size_t size;
    size_t position;
    uint32_t accumulator;
    int32_t count;
} saf_bits;

typedef struct saf_out_s {
    uint8_t *data;
    size_t capacity;
    size_t position;
} saf_out;

/* MSB first over a 32 bit accumulator.  Running out of input returns false,
 * which the token loop treats as "stop walking" -- exactly as the reference
 * does; the member is then rejected (or not) by the final size check. */
static bool saf_read(saf_bits *bits, int32_t nbits, uint32_t *value) {
    if (nbits == 0) {
        *value = 0U;
        return true;
    }
    while (bits->count < nbits) {
        if (bits->position >= bits->size) return false;
        bits->accumulator = (uint32_t)(bits->accumulator +
                                       ((uint32_t)bits->data[bits->position]
                                        << (24 - bits->count)));
        ++bits->position;
        bits->count += 8;
    }
    *value = bits->accumulator >> (32 - nbits);
    bits->accumulator = (uint32_t)(bits->accumulator << nbits);
    bits->count -= nbits;
    return true;
}

/* 2 bits; the value 3 escapes to 2 more bits, and the value 3 there escapes to
 * a chain of 4 bit nibbles terminated by a nibble below 15. */
static bool saf_read_length(saf_bits *bits, int32_t *length) {
    uint32_t value = 0U;
    int32_t result;
    if (!saf_read(bits, 2, &value)) return false;
    result = (int32_t)value;
    if (result == 3) {
        uint32_t extra = 0U;
        if (!saf_read(bits, 2, &extra)) return false;
        result += (int32_t)extra;
        if (extra == 3U) {
            for (;;) {
                uint32_t nibble = 0U;
                if (!saf_read(bits, 4, &nibble)) return false;
                result += (int32_t)nibble;
                if (nibble != 0xfU) break;
                if (result > 0x1000000) return false;
            }
        }
    }
    *length = result;
    return true;
}

static bool saf_put(saf_out *out, uint8_t *window, int32_t *index,
                    uint8_t byte) {
    if (out->position >= out->capacity) return false;
    window[*index] = byte;
    out->data[out->position] = byte;
    ++out->position;
    ++(*index);
    if (*index == SAF_WINDOW) *index = 0;
    return true;
}

/* Returns false only when the output would overflow.  A truncated bitstream
 * ends the walk without an error here, matching the reference: it stops the
 * loop and lets the whole-member size check decide.  Do not "fix" that into a
 * hard failure -- it would diverge from the shipped decoder. */
static bool saf_decode_chunk(const uint8_t *data, size_t size, saf_out *out) {
    saf_bits bits;
    uint8_t window[SAF_WINDOW];
    int32_t index = 0;

    bits.data = data;
    bits.size = size;
    bits.position = 0;
    bits.accumulator = 0U;
    bits.count = 0;
    xx_rt_memset(window, 0, sizeof(window));

    for (;;) {
        uint32_t token = 0U;
        uint32_t code;
        int32_t distance;
        int32_t length = 0;
        int32_t i;

        if (!saf_read(&bits, 9, &token)) break;
        if (token < 0x100U) {
            if (!saf_put(out, window, &index, (uint8_t)token)) return false;
            continue;
        }
        code = token & 0xffU;
        if (code == 0x81U) {
            distance = 1;
        } else if (code < 0x80U) {
            uint32_t low = 0U;
            if (!saf_read(&bits, 4, &low)) break;
            distance = (int32_t)(code * 16U + low);
        } else {
            distance = (int32_t)(code & 0x7fU);
            if (distance == 0) break; /* end of stream */
        }
        if (!saf_read_length(&bits, &length)) break;
        length += 2;
        for (i = 0; i < length; ++i) {
            /* Deliberate: the source index is taken modulo the ring, so a
             * distance larger than the bytes produced so far reads the ring's
             * stale (initially zero) content rather than failing.  The
             * reference does this and real members depend on it, so it is not
             * treated as a malformed back-reference.  A distance of 0 (code 0
             * with a zero low nibble) likewise repeats the byte at the write
             * cursor, exactly as the reference does. */
            uint8_t byte = window[(index - distance) & SAF_WINDOW_MASK];
            if (!saf_put(out, window, &index, byte)) return false;
        }
    }
    return true;
}

/* The whole member: one stream for method 3, a chain of length-prefixed
 * chunks otherwise.  Split out so the differential harness can drive exactly
 * this code to measure a stream's length before comparing bytes. */
static bool saf_decode_core(const uint8_t *input, size_t input_size,
                            uint32_t method, saf_out *outp) {
    saf_out out = *outp;
    bool ok = true;

    if (method == 3U) {
        ok = saf_decode_chunk(input, input_size, &out);
    } else {
        size_t position = 0;
        while (position < input_size) {
            uint32_t chunk_size;
            if ((input_size - position) < 4) {
                ok = false;
                break;
            }
            chunk_size = (uint32_t)input[position] |
                         ((uint32_t)input[position + 1] << 8) |
                         ((uint32_t)input[position + 2] << 16) |
                         ((uint32_t)input[position + 3] << 24);
            position += 4;
            /* The reference reads this length as a signed 32 bit value and
             * rejects a negative one, so the high bit is a hard error. */
            if (chunk_size > 0x7fffffffU) {
                ok = false;
                break;
            }
            if ((size_t)chunk_size > (input_size - position)) {
                ok = false;
                break;
            }
            if (!saf_decode_chunk(input + position, (size_t)chunk_size, &out)) {
                ok = false;
                break;
            }
            position += (size_t)chunk_size;
        }
    }

    *outp = out;
    return ok;
}

XXFC_API bool xx_saf_decode_memory_method(const uint8_t *input,
                                          size_t input_size, uint32_t method,
                                          uint8_t *output, size_t output_size,
                                          size_t *written) {
    saf_out out;

    if (written) *written = 0;
    if (!output && (output_size != 0)) return false;
    if (!input && (input_size != 0)) return false;

    out.data = output;
    out.capacity = output_size;
    out.position = 0;

    if (!saf_decode_core(input, input_size, method, &out)) return false;
    if (out.position != output_size) return false;
    if (written) *written = out.position;
    return true;
}

XXFC_API bool xx_saf_decode_memory(const uint8_t *input, size_t input_size,
                                   uint8_t *output, size_t output_size,
                                   size_t *written) {
    return xx_saf_decode_memory_method(input, input_size, 3U, output,
                                       output_size, written);
}
