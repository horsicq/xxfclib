/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_LZ4_H
#define XX_LZ4_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decodes one or more standard LZ4 frames into an exact-size memory buffer.
 * Embedded dictionaries are deliberately rejected because the caller has no
 * dictionary parameter. Block and content checksums are verified when present.
 */
XXFC_API bool xx_lz4_decompress_memory(const void *source, size_t source_size,
                                       void *destination, size_t destination_size,
                                       size_t *out_written);

/**
 * Decodes a single raw LZ4 block -- sequences only, with no frame header,
 * no block size prefix and no checksum.
 *
 * This is what a container supplies when it stores the block length itself
 * and compresses each block independently: SquashFS is the case in point,
 * since squashfs-tools emits bare LZ4_compress_default() output. Such data
 * is not a frame and will not decode through xx_lz4_decompress_memory().
 *
 * Unlike the frame entry point, the destination is a capacity rather than an
 * exact size: a block's decoded length is known only once it is decoded.
 * @param out_written receives the decoded length; may be NULL.
 */
XXFC_API bool xx_lz4_decompress_block(const void *source, size_t source_size,
                                      void *destination,
                                      size_t destination_capacity,
                                      size_t *out_written);

/**
 * Decodes LZ4 frames into a destination of at least the needed size.
 *
 * Unlike xx_lz4_decompress_memory(), @p destination_capacity is an upper
 * bound rather than the exact expected length. An LZ4 frame is not required
 * to carry a content size -- the lz4 CLI omits it by default -- so for such a
 * stream the output length simply is not knowable before decoding.
 * @param out_written receives the real decoded length.
 */
XXFC_API bool xx_lz4_decompress_frames(const void *source, size_t source_size,
                                       void *destination,
                                       size_t destination_capacity,
                                       size_t *out_written);


#ifdef __cplusplus
}
#endif

#endif /* XX_LZ4_H */
