/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_SCLSECTORS_H
#define XXFCLIB_ALGO_SCLSECTORS_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "SCL sectors" - the prefix-then-copy pseudo codec.
 *
 * It is not a compressor.  The member's bytes are stored verbatim, but the
 * file the user should get needs a header the container does not hold: SCL and
 * TR-DOS supply the 14-byte TR-DOS directory entry, CLP supplies a synthesised
 * BITMAPFILEHEADER, TI99 ARC supplies a TIFILES header for the members whose
 * payload is not compressed.  The reader hands that header over as the
 * record's compress-properties blob and the decode is
 *
 *     output = properties || stream
 *
 * with the declared uncompressed size acting as the check that both halves
 * arrived.  Three formats use it, which is why it is worth a module of its
 * own rather than being open-coded in each reader.
 *
 * The plaintext length is always known to the caller (it is prefix + stream),
 * so there is no scan entry point.
 */

/* Canonical form, for the prefix-less case: copies the stream verbatim.
 * Fails when @p output_size cannot hold all of @p input_size. */
XXFC_API bool xx_sclsectors_decode_memory(const uint8_t *input,
                                          size_t input_size, uint8_t *output,
                                          size_t output_size, size_t *written);

/* The real entry point: emit @p prefix (the record's compress-properties
 * blob, which may be empty) and then the stream verbatim.  Fails when the two
 * together do not fit in @p output_size.  @p written is set on every path. */
XXFC_API bool xx_sclsectors_decode_memory_ex(const uint8_t *prefix,
                                             size_t prefix_size,
                                             const uint8_t *input,
                                             size_t input_size,
                                             uint8_t *output,
                                             size_t output_size,
                                             size_t *written);

#ifdef __cplusplus
}
#endif
#endif
