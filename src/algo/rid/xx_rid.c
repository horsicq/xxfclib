/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Ported from XArchive/Algos/xriddecoder.cpp (XRidDecoder::decode / measure /
 * probe).  The reference contains no codec maths of its own: it frames the
 * chain and hands each packed block to XDclDecoder, so this port frames the
 * chain and hands each packed block to xx_dcl.
 *
 * DEVIATION, provably output-equivalent: the reference decodes a packed block
 * into a growable QByteArray and only afterwards checks the result against the
 * remaining budget.  A caller buffer cannot grow, so this port runs the same
 * block twice through the same core routine - xx_dcl_scan_memory to learn the
 * block's plaintext length within the budget, then xx_dcl_decode_memory for
 * exactly that many bytes.  xx_dcl's measure and decode share one routine and
 * cannot disagree, and the scan's limit is the same budget the reference
 * passes as maxOutputSize, so the accept/reject decision is identical.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/rid/xx_rid.h"

#include "xxfclib/algo/dcl/xx_dcl.h"

#define RID_FRAME_SIZE 3U
#define RID_TYPE_STORED 0x00U
#define RID_TYPE_PACKED 0x01U
#define RID_TYPE_END 0xffU

/* The frame length field is a u16, so this is a ceiling of the container, not
 * a policy limit. */
#define RID_MAX_BLOCK_SIZE 0xffffU
/* A member's uncompressed size lives in a u32 that the container itself caps
 * at 0x00ffffff. */
#define RID_MAX_MEMBER_SIZE 0x00ffffffU
/* A chain with more frames than this is not a RID member; the largest member
 * of the reference corpus uses 8 frames. */
#define RID_MAX_BLOCKS 100000U

/*
 * The chain walk, shared by both entry points.
 *
 * With @p output non-NULL the plaintext is written there and @p limit is the
 * member's declared size.  With @p output NULL nothing is kept - packed
 * blocks go through xx_dcl_scan_memory's sliding window - so a chain can be
 * measured before anything has been allocated for it.  One routine, so the
 * measure and the decode can never disagree about what a chain contains.
 */
static bool rid_chain(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t limit, size_t *consumed,
                      size_t *produced) {
    size_t offset = 0U;
    size_t out_at = 0U;
    unsigned blocks = 0U;

    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    if (!input) return false;

    while (blocks <= RID_MAX_BLOCKS) {
        size_t block_size;
        unsigned block_type;

        /* Frame read, bounded exactly as the reference bounds it: the frame
         * must lie whole inside the buffer. */
        if ((input_size < RID_FRAME_SIZE) ||
            (offset > input_size - RID_FRAME_SIZE)) {
            return false;
        }
        block_size = (size_t)input[offset] |
                     ((size_t)input[offset + 1U] << 8);
        block_type = input[offset + 2U];
        offset += RID_FRAME_SIZE;

        if (block_type == RID_TYPE_END) {
            /* The terminator is the only frame allowed to be empty, and it is
             * required to be empty: 0xff with a length would mean the walk is
             * reading something that is not a RID frame. */
            if (block_size != 0U) return false;
            if (consumed) *consumed = offset;
            if (produced) *produced = out_at;
            return true;
        }
        if ((block_type != RID_TYPE_STORED) &&
            (block_type != RID_TYPE_PACKED)) {
            return false;
        }
        if ((block_size == 0U) || (block_size > RID_MAX_BLOCK_SIZE)) {
            return false;
        }
        if (block_size > input_size - offset) return false;

        if (block_type == RID_TYPE_STORED) {
            if (block_size > limit - out_at) return false;
            if (output) {
                xx_rt_memcpy(output + out_at, input + offset, block_size);
            }
            out_at += block_size;
        } else {
            size_t budget = limit - out_at;
            size_t block_out = 0U;
            /* budget == 0 fails inside xx_dcl_scan_memory, which is what the
             * reference does too: XDclDecoder refuses maxOutputSize < 1. */
            if (!xx_dcl_scan_memory(input + offset, block_size, budget, NULL,
                                    &block_out)) {
                return false;
            }
            /* Deliberate, and matching the reference: XDclDecoder rejects a
             * stream that produces nothing at all (its `output.size < 1`
             * test), so a packed block that decodes to zero bytes fails the
             * whole member rather than being skipped. */
            if (block_out == 0U) return false;
            if (output) {
                size_t block_written = 0U;
                if (!xx_dcl_decode_memory(input + offset, block_size,
                                          output + out_at, block_out,
                                          &block_written) ||
                    (block_written != block_out)) {
                    return false;
                }
            }
            out_at += block_out;
        }

        offset += block_size;
        blocks++;
    }

    return false;
}

bool xx_rid_decode_memory(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          size_t *written) {
    size_t chain_size = 0U;
    size_t chain_out = 0U;

    if (written) *written = 0U;
    if (output_size > RID_MAX_MEMBER_SIZE) return false;
    if (!output && (output_size != 0U)) return false;
    if (!rid_chain(input, input_size, output, output_size, &chain_size,
                   &chain_out)) {
        return false;
    }
    /* The caller hands over the member's exact extent, so a chain that
     * terminates early means the member boundary and the frames disagree -
     * refuse rather than publish a truncated member. */
    if (chain_size != input_size) return false;
    if (chain_out != output_size) return false;
    if (written) *written = chain_out;
    return true;
}

bool xx_rid_scan_memory(const uint8_t *input, size_t input_size,
                        size_t max_output, size_t *consumed,
                        size_t *produced) {
    if (consumed) *consumed = 0U;
    if (produced) *produced = 0U;
    return rid_chain(input, input_size, NULL, max_output, consumed, produced);
}
