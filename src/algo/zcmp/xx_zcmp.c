/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ported from XArchive/Algos/xzcmpdecoder.cpp (XZcmpDecoder::decode).
 *
 * DEVIATION, provably output-equivalent: the reference drives zlib itself -
 * inflateInit2(&s, 15) per stream, inflate() in 64 KB chunks, then reads
 * stream.total_in back to find where the next stream begins.  This library has
 * no zlib, so the RFC 1950 wrapper is handled explicitly here and the deflate
 * payload goes to xx_deflate_unpack_memory_to_device_ex, which reports the
 * bytes its stream occupied.  The pieces map one to one:
 *
 *   zlib's window/format validation  -> the CMF/FLG checks below (CM == 8,
 *                                       CINFO <= 7, (CMF<<8|FLG) % 31 == 0,
 *                                       FDICT clear - zlib would answer
 *                                       Z_NEED_DICT, which the reference
 *                                       treats as failure);
 *   zlib's Adler-32 verification     -> xx_zcmp_adler32 over the block;
 *   stream.total_in at Z_STREAM_END  -> 2 + deflate consumed + 4;
 *   "never expand past the declared   -> the output device is opened over the
 *    size" chunk check                   remaining capacity only, and the
 *                                        deflate writer fails on a short write.
 *
 * ONE KNOWN GAP, stated rather than hidden: zlib also enforces the window size
 * that CINFO declares, refusing a back-reference that reaches further than
 * 256 << CINFO bytes.  xx_deflate always uses the full 32 KB window, so a
 * hand-built stream that declares a small window and then references beyond it
 * would be accepted here and rejected by zlib.  No encoder produces such a
 * stream - a deflater never emits a distance larger than the window it wrote
 * into the header - so this cannot change the result for any real member; it is
 * strictly a difference in how much a hostile stream is punished.
 */

#include "xxfclib/algo/zcmp/xx_zcmp.h"

#include "xxfclib/io/xx_io.h"

#define ZCMP_MAX_BLOCKS ((size_t)4000000)
#define ZCMP_MAX_OUTPUT ((size_t)0x7fffffff)
#define ZCMP_ZLIB_HEADER_SIZE ((size_t)2)
#define ZCMP_ZLIB_TRAILER_SIZE ((size_t)4)

static uint32_t xx_zcmp_adler32(const uint8_t *data, size_t size)
{
    uint32_t a = 1U;
    uint32_t b = 0U;
    size_t i;

    for (i = 0U; i < size; ++i) {
        a = (a + data[i]) % 65521U;
        b = (b + a) % 65521U;
    }

    return (b << 16) | a;
}

bool xx_zcmp_decode_memory(const uint8_t *input, size_t input_size,
                           uint8_t *output, size_t output_size,
                           size_t *written)
{
    size_t position = 0U;
    size_t produced = 0U;
    size_t blocks = 0U;

    if (written) *written = 0U;
    if (output_size > ZCMP_MAX_OUTPUT) return false;
    /* The reference answers true with an empty result before it even looks at
     * the packed bytes when the declared size is zero. */
    if (output_size == 0U) return true;
    if (!input || !output || (input_size == 0U)) return false;

    while ((produced < output_size) && (position < input_size)) {
        unsigned cmf;
        unsigned flg;
        size_t available;
        size_t consumed = 0U;
        size_t block_size;
        int64_t at;
        uint32_t stored;
        xx_io_device *device;
        bool ok;

        if (++blocks > ZCMP_MAX_BLOCKS) return false;

        if ((input_size - position) < ZCMP_ZLIB_HEADER_SIZE) return false;
        cmf = input[position];
        flg = input[position + 1U];
        if (((cmf & 0x0fU) != 8U) || ((cmf >> 4) > 7U) ||
            ((((cmf << 8) | flg) % 31U) != 0U) || ((flg & 0x20U) != 0U)) {
            return false;
        }

        available = input_size - position - ZCMP_ZLIB_HEADER_SIZE;
        if (available == 0U) return false;

        device = xx_io_mem_open(output + produced, output_size - produced);
        if (!device) return false;
        ok = xx_deflate_unpack_memory_to_device_ex(
            input + position + ZCMP_ZLIB_HEADER_SIZE, available, device,
            &consumed, false, NULL);
        at = xx_io_tell(device);
        xx_io_close(device);
        if (!ok || (at < 0)) return false;

        block_size = (size_t)at;
        if (block_size > (output_size - produced)) return false;
        if ((consumed == 0U) || (consumed > available)) return false;
        if ((available - consumed) < ZCMP_ZLIB_TRAILER_SIZE) return false;

        {
            const uint8_t *trailer = input + position + ZCMP_ZLIB_HEADER_SIZE +
                                     consumed;
            stored = ((uint32_t)trailer[0] << 24) |
                     ((uint32_t)trailer[1] << 16) |
                     ((uint32_t)trailer[2] << 8) | (uint32_t)trailer[3];
        }
        if (stored != xx_zcmp_adler32(output + produced, block_size)) {
            return false;
        }

        produced += block_size;
        position += ZCMP_ZLIB_HEADER_SIZE + consumed + ZCMP_ZLIB_TRAILER_SIZE;
    }

    if (produced != output_size) return false;

    if (written) *written = produced;

    return true;
}
