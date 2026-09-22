/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_FLSLZ_H
#define XXFCLIB_ALGO_FLSLZ_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode an IBM SaveRam / SaveRam2 "FLS" adaptive phrase stream.
 *
 * The stream starts with its own 'S' tag byte (0x53); pass the member extent
 * exactly as the container records it, tag included.
 *
 * The codec carries an explicit end code, so success means the end code was
 * reached.  As in the reference decoder, the whole input extent must be
 * consumed: the end code may leave up to seven padding bits in its final byte,
 * but no whole byte past that is valid inside a member's packed extent.  A
 * caller holding a stream with unknown extent should use
 * xx_flslz_scan_memory() instead.
 *
 * @param input       Compressed bytes, beginning with the 'S' tag.
 * @param input_size  Exact length of the member's packed extent.
 * @param output      Destination buffer.
 * @param output_size Capacity of @p output; overrunning it is a failure.
 * @param written     Receives the decoded size.  Set on every path.
 * @return true only on a complete decode that reached the end code.
 */
XXFC_API bool xx_flslz_decode_memory(const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_size,
                                     size_t *written);

/**
 * @brief Measure an FLS stream whose decoded size is not stored anywhere.
 *
 * Runs the very same decoder with the output discarded (the codec emits whole
 * dictionary phrases and never back-references the output, so discarding needs
 * no window at all).  Unlike the decode entry point this one does not require
 * the input to be exhausted: it reports how many bytes the stream actually
 * occupies, which lets a reader with no magic number demand that the stream end
 * exactly at the end of the file.
 *
 * @param input       Compressed bytes, beginning with the 'S' tag.
 * @param input_size  Bytes available; the stream may end before this.
 * @param max_output  Refuse a stream that would decode to more than this.
 * @param consumed    Receives the input bytes the stream occupies. May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 * @return true when the stream reached its end code within the limit.
 */
XXFC_API bool xx_flslz_scan_memory(const uint8_t *input, size_t input_size,
                                   size_t max_output, size_t *consumed,
                                   size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
