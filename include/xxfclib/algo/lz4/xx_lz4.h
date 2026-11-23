/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_LZ4_H
#define XX_LZ4_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decodes one or more standard LZ4 frames into an exact-size memory buffer.
 * Embedded dictionaries are deliberately rejected because the caller has no
 * dictionary parameter. Block and content checksums are verified when present.
 */
XXFC_API bool xx_lz4_decompress_memory(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written);
/** xx_lz4_decompress_frames for input that may end inside a frame (a cut
 * file): it then fails with `*cut` set and `*out_written` the bytes of the
 * whole blocks before that point, and of what there is of a cut uncompressed
 * block without a block checksum; the checksums still to come cannot check
 * them. `destination_size` bounds what the frames hold. Whole or cut,
 * `*checked` (optional) gets the bytes from the start that block and content
 * checksums vouch for, up to the first block or frame that none checks. */
XXFC_API bool xx_lz4_decompress_memory_prefix(const void *source, size_t source_size, void *destination, size_t destination_size, size_t *out_written, bool *cut,
                                              size_t *checked);

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
XXFC_API bool xx_lz4_decompress_block(const void *source, size_t source_size, void *destination, size_t destination_capacity, size_t *out_written);

/**
 * Decodes LZ4 frames into a destination of at least the needed size.
 *
 * Unlike xx_lz4_decompress_memory(), @p destination_capacity is an upper
 * bound rather than the exact expected length. An LZ4 frame is not required
 * to carry a content size -- the lz4 CLI omits it by default -- so for such a
 * stream the output length simply is not knowable before decoding.
 * @param out_written receives the real decoded length.
 */
XXFC_API bool xx_lz4_decompress_frames(const void *source, size_t source_size, void *destination, size_t destination_capacity, size_t *out_written);

/** Bound for standalone encoded output; zero on unsupported size/overflow. */
XXFC_API size_t xx_lz4_compress_bound(size_t source_size);
/** Encode a complete standard LZ4 frame. out_written is reset to zero on failure.
 * NULL source is accepted only for empty input. Input/output must not overlap. */
XXFC_API bool xx_lz4_compress_memory(const void *source,size_t source_size,void *destination,size_t destination_capacity,size_t *out_written);
/** Buffered device packing (up to INT32_MAX input bytes). offset<0 reads at
 * current position; exact size required. Devices remain owned by the caller.
 * Cancellation is checked between I/O chunks and before/after encoding. */
XXFC_API bool xx_lz4_pack_device(xx_io_device *source,int64_t offset,int64_t size,xx_io_device *destination,xx_pd_struct *pd);
/** Encode a raw LZ4 block (no Apple framing). Source size <= UINT32_MAX-1.
 * Bound is n+n/255+16. Final five bytes remain literals, per LZ4 specification. */
XXFC_API bool xx_lz4_compress_block(const void *source,size_t source_size,void *destination,size_t destination_capacity,size_t *out_written);

#ifdef __cplusplus
}
#endif

#endif /* XX_LZ4_H */
