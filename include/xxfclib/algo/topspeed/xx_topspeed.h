/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_TOPSPEED_H
#define XXFCLIB_ALGO_TOPSPEED_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TopSpeed (Clarion / JPI TopSpeed installer member) codec, ported from
 * XArchive/Algos/xtopspeeddecoder.cpp.
 *
 * A member is a CHAIN OF BLOCKS, not one stream.  Each block is a six byte
 * header - additive checksum (u16 LE), plaintext size (u16 LE), compressed
 * size (u16 LE) - followed by min(plain, compressed) bytes of payload.  When
 * the compressed size is NOT smaller than the plaintext size the block is
 * STORED and the payload is copied verbatim; otherwise the payload is a
 * 12-bit LZW stream with its own private dictionary, reset at every block.
 *
 * The codes are packed TWO PER THREE BYTES: a leading byte carries the high
 * nibbles of both codes (the first in its high nibble, the second in its low
 * nibble), then one low byte per code.
 *
 * NOTE: this is NOT the TPS codec (xx_tps_decode_memory).  Both formats come
 * out of the Clarion/TopSpeed lineage and both are called "TopSpeed", but TPS
 * is framed Yoshizaki LZHUF and shares no code with this one.
 */

/**
 * @brief Decode a complete TopSpeed member.
 *
 * The container stores no plaintext length of its own - the reader learns it
 * from xx_topspeed_scan_memory() - so @p output_size is checked against the
 * length the block headers add up to and a mismatch is a failure, exactly as
 * the reference does.  Every block header and every block checksum is verified
 * before a single byte is emitted.
 *
 * @param input       The member payload, starting at the first block header.
 * @param input_size  Length of @p input; the whole of it must be consumed.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected size, from xx_topspeed_scan_memory().
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true only when exactly @p output_size bytes were produced.
 */
XXFC_API bool xx_topspeed_decode_memory(const uint8_t *input,
                                        size_t input_size, uint8_t *output,
                                        size_t output_size, size_t *written);

/**
 * @brief Measure a TopSpeed member, which carries no stored plaintext length.
 *
 * Walks the block chain, validating every header and every additive checksum,
 * and sums the per-block plaintext sizes.  No LZW decoding happens here and
 * none is needed: each block states its own plaintext size in its header.
 *
 * The whole of @p input must be consumed exactly - a member that ends in the
 * middle of a block, or that has trailing bytes, is rejected.
 *
 * @param input       The member payload.
 * @param input_size  Length of @p input.
 * @param max_output  Refuse a member that would decode to more than this.
 * @param consumed    Receives the input bytes consumed (always @p input_size
 *                    on success). May be NULL.
 * @param produced    Receives the decoded size. May be NULL.
 * @return true when the whole member parsed and checksummed cleanly.
 */
XXFC_API bool xx_topspeed_scan_memory(const uint8_t *input, size_t input_size,
                                      size_t max_output, size_t *consumed,
                                      size_t *produced);

#ifdef __cplusplus
}
#endif
#endif
