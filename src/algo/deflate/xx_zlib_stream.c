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
#include "xxfclib/algo/adler32/xx_adler32.h"

#define XX_ZLIB_HEADER_SIZE 2U
#define XX_ZLIB_TRAILER_SIZE 4U
#define XX_ZLIB_CM_DEFLATE 8U
#define XX_ZLIB_FDICT 0x20U

size_t xx_zlib_stream_compress_bound(size_t source_size)
{
    size_t overhead = (source_size >> 12) + (source_size >> 14) +
                      (source_size >> 25) + 13U;
    if (source_size > SIZE_MAX - overhead) return SIZE_MAX;
    return source_size + overhead;
}

bool xx_zlib_stream_encode_memory(const uint8_t *input, size_t input_size,
                                  uint8_t *output, size_t output_capacity,
                                  size_t *written, int level)
{
    size_t payload_size = 0U;
    uint32_t adler;

    if (written) *written = 0U;
    if ((!input && input_size != 0U) || !output || output_capacity <
        XX_ZLIB_HEADER_SIZE + XX_ZLIB_TRAILER_SIZE) return false;

    /* CM=Deflate, CINFO=7 (32 KiB window). 0x9c advertises the normal
     * compression class and satisfies the RFC 1950 multiple-of-31 check. */
    output[0] = 0x78U;
    output[1] = 0x9cU;
    if (input_size == 0U) {
        /* A raw Deflate stream still needs a final block when its input is
         * empty. Emit one stored BFINAL block explicitly. */
        if (output_capacity < XX_ZLIB_HEADER_SIZE + 5U + XX_ZLIB_TRAILER_SIZE)
            return false;
        output[XX_ZLIB_HEADER_SIZE] = 0x01U;
        output[XX_ZLIB_HEADER_SIZE + 1U] = 0x00U;
        output[XX_ZLIB_HEADER_SIZE + 2U] = 0x00U;
        output[XX_ZLIB_HEADER_SIZE + 3U] = 0xffU;
        output[XX_ZLIB_HEADER_SIZE + 4U] = 0xffU;
        payload_size = 5U;
    } else if (!xx_deflate_compress_memory(input, input_size,
                                           output + XX_ZLIB_HEADER_SIZE,
                                           output_capacity - XX_ZLIB_HEADER_SIZE -
                                               XX_ZLIB_TRAILER_SIZE,
                                           &payload_size, level, false)) {
        return false;
    }
    adler = xx_zlib_stream_adler32(input ? input : (const uint8_t *)"", input_size);
    output[XX_ZLIB_HEADER_SIZE + payload_size] = (uint8_t)(adler >> 24);
    output[XX_ZLIB_HEADER_SIZE + payload_size + 1U] = (uint8_t)(adler >> 16);
    output[XX_ZLIB_HEADER_SIZE + payload_size + 2U] = (uint8_t)(adler >> 8);
    output[XX_ZLIB_HEADER_SIZE + payload_size + 3U] = (uint8_t)adler;
    if (written) *written = XX_ZLIB_HEADER_SIZE + payload_size + XX_ZLIB_TRAILER_SIZE;
    return true;
}

bool xx_zlib_stream_header_is_valid(const uint8_t *input, size_t input_size)
{
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

bool xx_zlib_stream_decode_memory(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_size, size_t *written)
{
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!xx_zlib_stream_header_is_valid(input, input_size)) return false;
    /* The Adler-32 trailer is not required to be present: a container that
     * stores the member's compressed length exactly may cut the stream at the
     * last Deflate byte. Only the header is subtracted here. */
    if (!xx_deflate_decompress_memory(input + XX_ZLIB_HEADER_SIZE, input_size - XX_ZLIB_HEADER_SIZE, output, output_size, &produced, false)) {
        return false;
    }
    if (written) *written = produced;
    return true;
}

uint32_t xx_zlib_stream_adler32(const uint8_t *data, size_t size)
{
    return xx_adler32(data, size);
}

bool xx_zlib_stream_trailer_matches(const uint8_t *input, size_t input_size, const uint8_t *plain, size_t plain_size)
{
    size_t at;
    uint32_t stored;

    if (!input || input_size < XX_ZLIB_HEADER_SIZE + XX_ZLIB_TRAILER_SIZE) {
        return false;
    }
    at = input_size - XX_ZLIB_TRAILER_SIZE;
    stored = ((uint32_t)input[at] << 24) | ((uint32_t)input[at + 1U] << 16) | ((uint32_t)input[at + 2U] << 8) | (uint32_t)input[at + 3U];
    return stored == xx_zlib_stream_adler32(plain, plain_size);
}
