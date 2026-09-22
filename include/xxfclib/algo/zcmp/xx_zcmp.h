/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_ZCMP_H
#define XXFCLIB_ALGO_ZCMP_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zcmp - the payload framing of the Solaris "compressed file" wrapper
 * (HANDLE_METHOD_ZCMP_BLOCKS).
 *
 * NO NEW CODEC, and nothing in common with xx_asymetrix / xx_genius beyond the
 * word "blocks": the DATA region handed in here is N complete zlib streams
 * (RFC 1950: 2-byte header, deflate data, 4-byte Adler-32) laid BACK TO BACK,
 * each inflating to exactly the container's block size except the last.
 *
 * TRAP - THERE ARE NO LENGTHS IN FRONT OF THE STREAMS.  A block ends where the
 * deflate data ends and the next one starts at the very next byte, so the only
 * way to find a boundary is to inflate and read back how much input the stream
 * consumed.  Handing the whole region to a one-shot zlib decoder would stop at
 * the end of the first block and report the first block's size as the answer.
 *
 * @p output_size IS the declared uncompressed size from the 40-byte header
 * (archives/xzcmparchive.cpp); the decode succeeds only when the concatenated
 * streams produce exactly that many bytes.  Trailing input after the last
 * needed block is tolerated, as in the reference. */
XXFC_API bool xx_zcmp_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);

#ifdef __cplusplus
}
#endif
#endif
