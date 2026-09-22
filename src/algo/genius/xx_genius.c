/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ported from XArchive/Algos/xgeniuslibrarydecoder.cpp
 * (XGeniusLibraryDecoder::decode).
 *
 * DEVIATIONS, both provably output-equivalent:
 *
 * 1. The reference calls XDclDecoder::decode(block, &plain, remaining), which
 *    grows a QByteArray.  Writing straight into the caller's buffer needs the
 *    block's plaintext length up front, so each block is first measured with
 *    xx_dcl_scan_memory and then decoded with xx_dcl_decode_memory.  Both are
 *    the same core routine (dcl_run) over the same bytes with the same limit,
 *    so the measure and the decode cannot disagree - that is exactly the
 *    property the scan entry point exists to provide.
 * 2. The reference computes the trailer CRC with XBinary::_getCRC32, which is
 *    a table lookup; the identical reflected EDB88320 CRC is computed here from
 *    a table built on the stack (no module-level mutable state).
 */

#include "xxfclib/algo/genius/xx_genius.h"

#include "xxfclib/algo/dcl/xx_dcl.h"

/* uint32 plaintext length + uint32 CRC-32 behind the last block. */
#define GPL_TRAILER_SIZE ((size_t)8)
/* The block length field in front of every block. */
#define GPL_BLOCKLEN_SIZE ((size_t)4)
/* A DCL stream cannot be shorter than its two prelude bytes plus one byte
 * holding the start of the end-of-stream code. */
#define GPL_MIN_BLOCK_SIZE ((size_t)3)
/* The reader will not publish a member larger than this either. */
#define GPL_MAX_OUTPUT ((size_t)512 * 1024 * 1024)

static uint32_t xx_genius_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* DELIBERATE: the stored value is the RUNNING EDB88320 accumulator seeded with
 * 0xFFFFFFFF, NOT the customary complemented result.  The writer stores the
 * unfinalised accumulator and XBinary::_getCRC32 is that same unfinalised
 * primitive, so there is no final "^ 0xFFFFFFFF" here.  Do not "fix" it. */
static uint32_t xx_genius_crc32_unfinalised(const uint8_t *data, size_t size)
{
    uint32_t table[256];
    uint32_t crc = 0xFFFFFFFFU;
    uint32_t i;
    size_t at;

    for (i = 0U; i < 256U; ++i) {
        uint32_t value = i;
        unsigned bit;
        for (bit = 0U; bit < 8U; ++bit) {
            value = (value & 1U) ? (0xEDB88320U ^ (value >> 1)) : (value >> 1);
        }
        table[i] = value;
    }

    for (at = 0U; at < size; ++at) {
        crc = table[(crc ^ data[at]) & 0xFFU] ^ (crc >> 8);
    }

    return crc;
}

bool xx_genius_decode_memory(const uint8_t *input, size_t input_size,
                             uint8_t *output, size_t output_size,
                             size_t *written)
{
    size_t blocks_end;
    size_t offset = 0U;
    size_t produced = 0U;
    uint32_t declared_size;
    uint32_t stored_crc;

    if (written) *written = 0U;
    if (!input || !output) return false;
    /* The reference rejects a zero-length member outright: a compressed member
     * always has at least one block. */
    if ((output_size < 1U) || (output_size > GPL_MAX_OUTPUT)) return false;
    if (input_size < (GPL_TRAILER_SIZE + GPL_BLOCKLEN_SIZE +
                      GPL_MIN_BLOCK_SIZE)) {
        return false;
    }

    blocks_end = input_size - GPL_TRAILER_SIZE;

    /* Every iteration consumes at least GPL_BLOCKLEN_SIZE + GPL_MIN_BLOCK_SIZE
     * bytes, so the walk always reaches blocks_end; there is no block-count cap
     * because the writer's block size is not part of the format and capping on
     * an assumed one would reject a legal member. */
    while (offset < blocks_end) {
        size_t block_size;
        size_t remaining;
        size_t plain_size = 0U;
        size_t block_written = 0U;

        if ((blocks_end - offset) < GPL_BLOCKLEN_SIZE) return false;
        block_size = (size_t)xx_genius_read_le32(input + offset);
        offset += GPL_BLOCKLEN_SIZE;
        if ((block_size < GPL_MIN_BLOCK_SIZE) ||
            (block_size > (blocks_end - offset))) {
            return false;
        }

        remaining = output_size - produced;
        if (remaining < 1U) return false;

        /* The plaintext still outstanding doubles as this block's ceiling, so a
         * block that wants to produce more than the member promises stops right
         * there instead of overrunning. */
        if (!xx_dcl_scan_memory(input + offset, block_size, remaining, NULL,
                                &plain_size)) {
            return false;
        }
        if ((plain_size == 0U) || (plain_size > remaining)) return false;
        if (!xx_dcl_decode_memory(input + offset, block_size,
                                  output + produced, plain_size,
                                  &block_written) ||
            (block_written != plain_size)) {
            return false;
        }

        produced += plain_size;
        offset += block_size;
    }

    /* The blocks have to end exactly where the trailer begins. */
    if (offset != blocks_end) return false;
    if (produced != output_size) return false;

    declared_size = xx_genius_read_le32(input + blocks_end);
    stored_crc = xx_genius_read_le32(input + blocks_end + 4U);
    if ((size_t)declared_size != output_size) return false;

    /* Over the 433 members of the reference corpus the stored CRC matches on
     * every compressed member, so it is enforced: publishing bytes that fail it
     * would be worse than failing. */
    if (xx_genius_crc32_unfinalised(output, output_size) != stored_crc) {
        return false;
    }

    if (written) *written = produced;

    return true;
}
