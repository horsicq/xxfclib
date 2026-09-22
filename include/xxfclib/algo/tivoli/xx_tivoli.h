/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_TIVOLI_H
#define XXFCLIB_ALGO_TIVOLI_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Unwrap a Tivoli Filepack Block (.PKT) container.
 *
 * @p input is the WHOLE container -- the 79-byte ASCII header line and the
 * block chain behind it -- and @p output receives the WHOLE unwrapped stream.
 * That is not a convenience: a member's bytes exist nowhere in the file, the
 * chain is one stream whose blocks are not aligned to members, and every
 * block's last eight bytes are a running-MD5 tail rather than payload, so no
 * member can be decoded without decoding every block before it.  A reader
 * slices its member out of the result with the {u32 offset, u32 size} pair the
 * container's member index carries.
 *
 * The running MD5 tail of every block is verified; it is the only integrity
 * signal the format has.
 *
 * @param input       The whole container.
 * @param input_size  Length of @p input.
 * @param output      Destination for the unwrapped stream.
 * @param output_size Capacity, and the exact expected unwrapped length.
 * @param written     Receives the produced length.  Set on every path.
 * @return true on a complete, digest-verified unwrap that filled
 *         @p output_size exactly.
 */
XXFC_API bool xx_tivoli_decode_memory(const uint8_t *input, size_t input_size,
                                      uint8_t *output, size_t output_size,
                                      size_t *written);

/**
 * @brief Measure the unwrapped stream of a Tivoli container.
 *
 * The container stores each member's size but never the length of the whole
 * unwrapped stream, so a reader that wants the stream as one buffer has to
 * measure it.  This runs the same core with the output discarded, so the
 * measurement and the decode can never disagree.
 *
 * @param input       The whole container.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a container that would unwrap to more than this.
 * @param consumed    Receives the input bytes the chain occupies (the offset
 *                    of the end-of-chain marker, or of the trailing odd byte).
 *                    May be NULL.
 * @param produced    Receives the unwrapped size.  May be NULL.
 * @return true when the whole chain unwrapped and verified within the limit.
 */
XXFC_API bool xx_tivoli_scan_memory(const uint8_t *input, size_t input_size,
                                    size_t max_output, size_t *consumed,
                                    size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
