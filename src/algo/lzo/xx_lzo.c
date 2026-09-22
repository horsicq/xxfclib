/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * A bounds-checked implementation of the LZO1X token grammar used by lzop.
 * It deliberately receives a pre-sized output buffer: callers therefore
 * retain control of expansion limits before any match is materialized.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/lzo/xx_lzo.h"

#include <string.h>

typedef struct xx_lzo_reader_s {
    const uint8_t *input;
    size_t input_size;
    size_t input_position;
    uint8_t *output;
    size_t output_size;
    size_t output_position;
} xx_lzo_reader;

static bool xx_lzo_take(xx_lzo_reader *stream, uint8_t *value) {
    if (!stream || !value || stream->input_position >= stream->input_size) {
        return false;
    }
    *value = stream->input[stream->input_position++];
    return true;
}

static bool xx_lzo_copy_literals(xx_lzo_reader *stream, size_t length) {
    if (!stream || length > stream->input_size - stream->input_position ||
        length > stream->output_size - stream->output_position) {
        return false;
    }
    if (length != 0U) {
        xx_rt_memcpy(stream->output + stream->output_position,
               stream->input + stream->input_position, length);
    }
    stream->input_position += length;
    stream->output_position += length;
    return true;
}

static bool xx_lzo_copy_match(xx_lzo_reader *stream, size_t distance,
                              size_t length) {
    size_t index;
    if (!stream || distance == 0U || distance > stream->output_position ||
        length > stream->output_size - stream->output_position) {
        return false;
    }
    /* Forward byte copies intentionally preserve LZ-style overlap. */
    for (index = 0U; index < length; ++index) {
        stream->output[stream->output_position] =
            stream->output[stream->output_position - distance];
        ++stream->output_position;
    }
    return true;
}

/* Decode the zero-extended lengths shared by literal, M3, and M4 tokens.
 *
 * A non-zero field in the token is the length itself.  A zero field means the
 * length continues in the byte stream: every following zero byte adds 255, and
 * the first non-zero byte ends the run and contributes base + itself.  The
 * accumulator must NOT be used as the loop condition - once the first zero
 * byte has pushed it to 255 it is no longer zero, and stopping there both
 * truncates the length and leaves the terminating byte unconsumed. */
static bool xx_lzo_extended_length(xx_lzo_reader *stream, uint64_t initial,
                                   uint64_t base, uint64_t *length) {
    uint64_t value = initial;
    uint8_t byte;
    if (!stream || !length) return false;
    if (value != 0U) {
        *length = value;
        return true;
    }
    for (;;) {
        if (!xx_lzo_take(stream, &byte)) return false;
        if (byte != 0U) {
            if (value > UINT64_MAX - base - byte) return false;
            value += base + byte;
            *length = value;
            return true;
        }
        if (value > UINT64_MAX - 255U) return false;
        value += 255U;
    }
}

static bool xx_lzo_to_size(uint64_t value, size_t *result) {
    if (!result || value > (uint64_t)SIZE_MAX) return false;
    *result = (size_t)value;
    return true;
}

bool xx_lzo1x_decompress(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written) {
    xx_lzo_reader stream;
    uint8_t token;
    bool match_token = false;
    bool after_literal = false;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || (!output && output_size != 0U) ||
        input_size == 0U) {
        return false;
    }
    xx_rt_memset(&stream, 0, sizeof(stream));
    stream.input = input;
    stream.input_size = input_size;
    stream.output = output;
    stream.output_size = output_size;
    if (!xx_lzo_take(&stream, &token)) return false;

    /* Streams can start with a literal run.  Short initial runs instead use
     * the normal post-match token path defined by LZO1X. */
    if (token > 17U) {
        size_t literal_count = (size_t)(token - 17U);
        if (!xx_lzo_copy_literals(&stream, literal_count) ||
            !xx_lzo_take(&stream, &token)) {
            return false;
        }
        match_token = true;
        after_literal = literal_count >= 4U;
    }

    for (;;) {
        size_t distance;
        size_t length;
        uint8_t trailing_literals = 0U;

        if (!match_token) {
            uint64_t literal_length;
            if (token >= 16U) {
                match_token = true;
                after_literal = false;
                continue;
            }
            if (!xx_lzo_extended_length(&stream, token, 15U,
                                        &literal_length) ||
                literal_length > UINT64_MAX - 3U ||
                !xx_lzo_to_size(literal_length + 3U, &length) ||
                !xx_lzo_copy_literals(&stream, length) ||
                !xx_lzo_take(&stream, &token)) {
                return false;
            }
            match_token = true;
            after_literal = true;
            continue;
        }

        if (token < 16U && after_literal) {
            uint8_t low;
            distance = (size_t)UINT16_C(0x801) + (size_t)(token >> 2U);
            if (!xx_lzo_take(&stream, &low)) return false;
            if ((uint64_t)distance + ((uint64_t)low << 2U) >
                (uint64_t)SIZE_MAX) {
                return false;
            }
            distance += (size_t)low << 2U;
            length = 3U;
            trailing_literals = token & 3U;
        } else if (token >= 64U) {
            uint8_t low;
            if (!xx_lzo_take(&stream, &low)) return false;
            distance = 1U + (size_t)((token >> 2U) & 7U) +
                       ((size_t)low << 3U);
            length = (size_t)(token >> 5U) + 1U;
            trailing_literals = token & 3U;
        } else if (token >= 32U) {
            uint64_t match_length;
            uint8_t high;
            uint8_t low;
            if (!xx_lzo_extended_length(&stream, token & 31U, 31U,
                                        &match_length) ||
                match_length > UINT64_MAX - 2U ||
                !xx_lzo_to_size(match_length + 2U, &length) ||
                !xx_lzo_take(&stream, &high) ||
                !xx_lzo_take(&stream, &low)) {
                return false;
            }
            distance = 1U + (size_t)(high >> 2U) + ((size_t)low << 6U);
            trailing_literals = high & 3U;
        } else if (token >= 16U) {
            uint64_t match_length;
            uint8_t high;
            uint8_t low;
            uint64_t encoded_distance;
            if (!xx_lzo_extended_length(&stream, token & 7U, 7U,
                                        &match_length) ||
                match_length > UINT64_MAX - 2U ||
                !xx_lzo_to_size(match_length + 2U, &length) ||
                !xx_lzo_take(&stream, &high) ||
                !xx_lzo_take(&stream, &low)) {
                return false;
            }
            encoded_distance = ((uint64_t)(token & 8U) << 11U) |
                               (uint64_t)(high >> 2U) |
                               ((uint64_t)low << 6U);
            if (encoded_distance == 0U) {
                if (stream.input_position != stream.input_size) return false;
                if (written) *written = stream.output_position;
                return true;
            }
            if (encoded_distance > (uint64_t)SIZE_MAX - UINT16_C(0x4000)) {
                return false;
            }
            distance = (size_t)encoded_distance + UINT16_C(0x4000);
            trailing_literals = high & 3U;
        } else {
            uint8_t low;
            if (!xx_lzo_take(&stream, &low)) return false;
            distance = 1U + (size_t)(token >> 2U) + ((size_t)low << 2U);
            length = 2U;
            trailing_literals = token & 3U;
        }

        if (!xx_lzo_copy_match(&stream, distance, length)) return false;
        if (trailing_literals == 0U) {
            if (!xx_lzo_take(&stream, &token)) return false;
            match_token = false;
            after_literal = false;
            continue;
        }
        if (!xx_lzo_copy_literals(&stream, trailing_literals) ||
            !xx_lzo_take(&stream, &token)) {
            return false;
        }
        match_token = true;
        after_literal = false;
    }
}
