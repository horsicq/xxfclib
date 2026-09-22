/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TPS member framing, ported from XArchive/Algos/xtpsdecoder.cpp.  The codec
 * underneath is the shared LZHUF in src/algo/lzhuf, whose plain parameter set
 * is bit-for-bit the one the reference asks XLZHUFDecoder::getOptions() for
 * (dist variant 1, F 0x3C, THRESHOLD 2, no EOF symbol, MAX_FREQ 0x8000, ring
 * 0x2000 prefilled with 0x20).
 *
 * Deliberately kept from the reference, do not "fix":
 *
 *  - The coded length is not stored anywhere; it is SOLVED for.  With
 *    m = coded + 4 and q = ceil(m / 0x4000) blocks, the on-disk size is
 *    m - 3 + q, so the loop looks for the q that is self consistent and then
 *    re-derives the on-disk size to confirm it.  Taking the first such q is
 *    correct: on-disk size is strictly decreasing in q for a fixed m, so at
 *    most one q can satisfy it.
 *  - The block counter starts at 4, not 0, and the running checksum starts at
 *    the sum of the four little-endian bytes of the DECODED length.  Those
 *    four bytes are never in the payload; they live in the directory entry.
 *  - The check byte is plain, NOT XOR 0x80, while every data byte is XORed.
 *  - The whole input must be consumed exactly, and the final byte must be the
 *    0x01 end marker.  Trailing bytes are a failure, not slack.
 *
 * One intentional difference: the reference refuses nUncompressedSize above
 * XTPSDecoder::MAX_UNCOMPRESSED_SIZE (0x40000000).  That is a container sanity
 * limit, not part of the codec, and this library caps the decoded size against
 * the caller's output_size only.  The one size ceiling kept is 32 bits,
 * because the length is fed to the checksum as a dword and a wider value would
 * change the framing rather than merely the allocation.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/algo/lzhuf/xx_lzhuf.h"
#include "xxfclib/algo/tps/xx_tps.h"

#define TPS_BLOCK_SIZE 0x4000

/* On-disk length of a member whose coded payload is `coded` bytes: the coded
 * bytes, one check byte per block, and the 0x01 end marker.  The +4 is the
 * uncompressed-size dword that block 0 is charged for. */
static size_t tps_stored_size(size_t coded)
{
    size_t blocks = (coded + 4U + (size_t)(TPS_BLOCK_SIZE - 1)) /
                    (size_t)TPS_BLOCK_SIZE;

    return coded + blocks + 1U;
}

/* Undo the XOR, verify every block checksum and the end marker.  `output` must
 * hold tps_solve_coded()'s result. */
static bool tps_dechunk(const uint8_t *input, size_t input_size,
                        uint32_t decoded_size, size_t coded, uint8_t *output)
{
    size_t pos = 0U;
    size_t left = coded;
    size_t produced = 0U;
    size_t in_block = 4U; /* the size dword at directory offset +0x12 */
    uint8_t check = 0U;
    int shift;

    for (shift = 0; shift < 32; shift += 8) {
        check = (uint8_t)(check + (uint8_t)((decoded_size >> shift) & 0xffU));
    }

    for (;;) {
        while ((left > 0U) && (in_block != (size_t)TPS_BLOCK_SIZE)) {
            uint8_t byte;

            if (pos >= input_size) return false;
            byte = (uint8_t)(input[pos] ^ 0x80U);
            pos++;
            output[produced] = byte;
            produced++;
            check = (uint8_t)(check + byte);
            in_block++;
            left--;
        }
        if (pos >= input_size) return false;
        if (input[pos] != check) return false;
        pos++;
        if (left == 0U) {
            if (pos >= input_size) return false;
            if (input[pos] != 1U) return false;
            pos++;
            break;
        }
        in_block = 0U;
        check = 0U;
    }

    return pos == input_size;
}

static bool tps_solve_coded(size_t input_size, size_t *coded_out)
{
    size_t max_blocks;
    size_t q;

    if (input_size < 2U) return false;

    max_blocks = ((input_size + 3U) / (size_t)TPS_BLOCK_SIZE) + 2U;

    for (q = 1U; q <= max_blocks; ++q) {
        size_t m;

        if (q > (input_size + 3U)) break;
        m = input_size + 3U - q;
        if (m < 4U) break;
        if (((m + (size_t)(TPS_BLOCK_SIZE - 1)) / (size_t)TPS_BLOCK_SIZE) == q) {
            if (tps_stored_size(m - 4U) != input_size) return false;
            *coded_out = m - 4U;
            return true;
        }
    }

    return false;
}

bool xx_tps_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size, size_t *written)
{
    uint8_t *coded_buffer;
    size_t coded = 0U;
    uint32_t decoded_size;
    bool ok;

    if (written) *written = 0U;
    if (!output) return false;
    if (!input && (input_size != 0U)) return false;

    decoded_size = (uint32_t)output_size;
    if ((size_t)decoded_size != output_size) return false;

    if (!tps_solve_coded(input_size, &coded)) return false;

    /* xx_mem_alloc(0) is not a usable pointer; a zero-length coded payload is
     * a legal member (an empty file) and its framing is still checked. */
    coded_buffer = (uint8_t *)xx_mem_alloc(coded ? coded : 1U);
    if (!coded_buffer) return false;

    ok = tps_dechunk(input, input_size, decoded_size, coded, coded_buffer);
    if (ok) {
        ok = xx_lzhuf_decode_memory(coded_buffer, coded, output, output_size,
                                    written);
    }

    xx_rt_memset(coded_buffer, 0, coded ? coded : 1U);
    xx_mem_free(coded_buffer);

    if (!ok && written) *written = 0U;

    return ok;
}
