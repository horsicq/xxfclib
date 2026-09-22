/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ported from XArchive/Algos/xasymetrixdecoder.cpp (XAsymetrixDecoder::decode).
 *
 * DEVIATION, provably output-equivalent: the reference carries its own private
 * copy of the blast-derived DCL explode (asymDclExplode) inside the file.  This
 * port calls xx_dcl_decode_memory instead.  The two are the same algorithm with
 * the same fixed tables - identical literal/length/distance run tables, the
 * same inverted canonical code assembly, the same 519 end code, the same
 * "distance may not reach back before the start of THIS block" rule (the
 * reference bounds it by nBase, xx_dcl by output_at, and each block is handed
 * its own output region here) - and both refuse a stream that does not produce
 * its implied length exactly.  Nothing in the reference's copy differs from the
 * library's beyond spelling.
 */

#include "xxfclib/algo/asymetrix/xx_asymetrix.h"

#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/rt/xx_rt.h"

#define ASYM_BLOCK_HEADER_SIZE ((size_t)6)
#define ASYM_BLOCK_UNPACKED_SIZE ((size_t)4096)
/* The corpus never exceeds 4096 packed bytes per block; the wider ceiling only
 * keeps a hypothetical expanding block readable while still bounding what a
 * malformed length field can ask the walk to skip. */
#define ASYM_MAX_BLOCK_SIZE ((size_t)0x10000)
#define ASYM_MAX_OUTPUT ((size_t)0x7fffffff)
#define ASYM_METHOD_STORED 0U
#define ASYM_METHOD_IMPLODE 1U

bool xx_asymetrix_decode_memory(const uint8_t *input, size_t input_size,
                                uint8_t *output, size_t output_size,
                                size_t *written)
{
    size_t offset = 0U;
    size_t produced = 0U;

    if (written) *written = 0U;
    if (!input && (input_size > 0U)) return false;
    if (!output && (output_size > 0U)) return false;
    /* The reference refuses a member larger than qint32 because its result is
     * a QByteArray; keep the same ceiling so the two agree on every input. */
    if (output_size > ASYM_MAX_OUTPUT) return false;

    while (produced < output_size) {
        unsigned method;
        size_t block_size;
        size_t payload;
        size_t wanted;

        if ((input_size < ASYM_BLOCK_HEADER_SIZE) ||
            (offset > input_size - ASYM_BLOCK_HEADER_SIZE)) {
            return false;
        }

        method = (unsigned)input[offset] |
                 ((unsigned)input[offset + 1U] << 8);
        block_size = (size_t)input[offset + 2U] |
                     ((size_t)input[offset + 3U] << 8) |
                     ((size_t)input[offset + 4U] << 16) |
                     ((size_t)input[offset + 5U] << 24);
        payload = offset + ASYM_BLOCK_HEADER_SIZE;

        if ((block_size > ASYM_MAX_BLOCK_SIZE) ||
            (block_size > input_size - payload)) {
            return false;
        }

        wanted = output_size - produced;
        if (wanted > ASYM_BLOCK_UNPACKED_SIZE) wanted = ASYM_BLOCK_UNPACKED_SIZE;

        if (method == ASYM_METHOD_STORED) {
            /* DELIBERATE: a stored block must be exactly the implied unpacked
             * length, not merely fit.  This is what refuses the 6-zero-byte
             * end-of-volume trailer, i.e. a member whose remaining blocks live
             * on the next disk of the set.  Do not relax it. */
            if (block_size != wanted) return false;
            if (block_size > 0U) {
                xx_rt_memcpy(output + produced, input + payload, block_size);
            }
        } else if (method == ASYM_METHOD_IMPLODE) {
            size_t block_written = 0U;
            if (!xx_dcl_decode_memory(input + payload, block_size,
                                      output + produced, wanted,
                                      &block_written) ||
                (block_written != wanted)) {
                return false;
            }
        } else {
            return false;
        }

        produced += wanted;
        offset = payload + block_size;
    }

    /* The producing class sizes the stream to the chain exactly, so anything
     * left over means the reader and the codec disagree about the framing. */
    if ((offset != input_size) || (produced != output_size)) return false;

    if (written) *written = produced;

    return true;
}
