/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_zlib_stream.c
 * @brief RFC 1950 zlib wrapper around the raw Deflate decoder.
 *
 * A zlib stream is a two-byte header, a raw Deflate stream, and a four-byte
 * big-endian Adler-32 of the decoded bytes. Enough containers store their
 * members this way that each one open-coding the header check would be eight
 * copies of the same two lines, differing only in which of them forgot the
 * FDICT case.
 */

#include "xxfclib/algo/deflate/xx_deflate.h"

#define XX_ZLIB_HEADER_SIZE 2U
#define XX_ZLIB_TRAILER_SIZE 4U
#define XX_ZLIB_CM_DEFLATE 8U
#define XX_ZLIB_FDICT 0x20U

bool xx_zlib_stream_header_is_valid(const uint8_t *input, size_t input_size) {
    uint8_t cmf;
    uint8_t flg;

    if (!input || input_size < XX_ZLIB_HEADER_SIZE) return false;
    cmf = input[0];
    flg = input[1];
    /* Only method 8 is defined, and a window larger than 32 KiB is not. */
    if ((cmf & 0x0FU) != XX_ZLIB_CM_DEFLATE) return false;
    if ((cmf >> 4) > 7U) return false;
    /* The two header bytes read as a big-endian u16 must be a multiple of 31:
     * that check is what makes a two-byte header worth anything at all. */
    if ((((uint16_t)cmf << 8) | (uint16_t)flg) % 31U != 0U) return false;
    /* A preset dictionary would need a dictionary this decoder has no way to
     * obtain, so a stream asking for one cannot be decoded, only refused. */
    if (flg & XX_ZLIB_FDICT) return false;
    return true;
}

bool xx_zlib_stream_decode_memory(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_size,
                                  size_t *written) {
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!xx_zlib_stream_header_is_valid(input, input_size)) return false;
    /* The Adler-32 trailer is not required to be present: a container that
     * stores the member's compressed length exactly may cut the stream at the
     * last Deflate byte. Only the header is subtracted here. */
    if (!xx_deflate_decompress_memory(input + XX_ZLIB_HEADER_SIZE,
                                      input_size - XX_ZLIB_HEADER_SIZE, output,
                                      output_size, &produced, false)) {
        return false;
    }
    if (written) *written = produced;
    return true;
}

uint32_t xx_zlib_stream_adler32(const uint8_t *data, size_t size) {
    uint32_t a = 1U;
    uint32_t b = 0U;
    size_t i;

    for (i = 0U; i < size; ++i) {
        a = (a + data[i]) % 65521U;
        b = (b + a) % 65521U;
    }
    return (b << 16) | a;
}

bool xx_zlib_stream_trailer_matches(const uint8_t *input, size_t input_size,
                                    const uint8_t *plain, size_t plain_size) {
    size_t at;
    uint32_t stored;

    if (!input || input_size < XX_ZLIB_HEADER_SIZE + XX_ZLIB_TRAILER_SIZE) {
        return false;
    }
    at = input_size - XX_ZLIB_TRAILER_SIZE;
    stored = ((uint32_t)input[at] << 24) | ((uint32_t)input[at + 1U] << 16) |
             ((uint32_t)input[at + 2U] << 8) | (uint32_t)input[at + 3U];
    return stored == xx_zlib_stream_adler32(plain, plain_size);
}
