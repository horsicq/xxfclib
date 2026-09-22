/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lzfse.h @brief LZFSE / LZVN decoder. */

/* LZFSE is Apple's general purpose compressor, published under a BSD licence
 * at github.com/lzfse/lzfse.  It is what a modern DMG uses for its 0x80000007
 * runs, and what the Apple platforms' `compression_encode_buffer` writes.
 *
 * A stream is a sequence of blocks, each opening with a four byte magic read
 * little endian.  The magics, and nothing else, decide how the block body is
 * read:
 *
 *   bvx$  0x24787662  end of stream; the block is just the magic
 *   bvx-  0x2d787662  raw bytes: magic, u32 n_raw_bytes, then the bytes
 *   bvx1  0x31787662  FSE compressed, frequency tables stored verbatim
 *   bvx2  0x32787662  FSE compressed, frequency tables bit-packed
 *   bvxn  0x6e787662  LZVN compressed; the encoder picks this for short
 *                     blocks, where the FSE tables would cost more than
 *                     they save
 *
 * An FSE block carries four interleaved literal streams and one combined
 * L/M/D (literal-run length, match length, match distance) stream.  Both are
 * read BACKWARDS from the end of their payload, which is why the header
 * carries the encoder's final states.
 *
 * Everything here is little endian, unlike the UDIF container that carries
 * it.  Nothing is allocated in proportion to a declared output size: the
 * caller supplies the destination and its capacity is the only ceiling.
 */

#ifndef XXFCLIB_ALGO_LZFSE_H
#define XXFCLIB_ALGO_LZFSE_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The four block magics, little endian, as they sit in the stream. */
#define XX_LZFSE_MAGIC_ENDOFSTREAM UINT32_C(0x24787662) /* bvx$ */
#define XX_LZFSE_MAGIC_UNCOMPRESSED UINT32_C(0x2d787662) /* bvx- */
#define XX_LZFSE_MAGIC_COMPRESSEDV1 UINT32_C(0x31787662) /* bvx1 */
#define XX_LZFSE_MAGIC_COMPRESSEDV2 UINT32_C(0x32787662) /* bvx2 */
#define XX_LZFSE_MAGIC_COMPRESSEDLZVN UINT32_C(0x6e787662) /* bvxn */

/** @brief Returns true; the decoder is built in and needs no external library. */
XXFC_API bool xx_lzfse_is_available(void);

/**
 * @brief Whether @p source opens with a plausible LZFSE block magic.
 *
 * Only the first four bytes are examined, so this is a prefilter and not a
 * validation: a stream that passes here may still fail to decode.
 */
XXFC_API bool xx_lzfse_header_is_valid(const void *source, size_t source_size);

/**
 * @brief Decode a complete LZFSE stream into a caller supplied buffer.
 *
 * Every block of the stream is decoded until the end-of-stream marker is
 * reached.  A stream that runs past @p destination_size, that ends without
 * the marker, or that carries a block the decoder cannot read, fails.
 *
 * @param out_written receives the number of bytes produced.  It is set on
 *        success only; its value after a failure is not meaningful.
 * @return true when the whole stream decoded and the marker was reached.
 */
XXFC_API bool xx_lzfse_decompress_memory(const void *source,
                                         size_t source_size,
                                         void *destination,
                                         size_t destination_size,
                                         size_t *out_written);

/**
 * @brief Decode one bare LZVN payload, without the surrounding bvxn header.
 *
 * This is the inner layer of a `bvxn` block, exposed on its own because LZVN
 * also appears outside LZFSE.  Decoding stops at the LZVN end-of-stream
 * opcode; matches may not reach before @p destination.
 *
 * @param out_written receives the number of bytes produced.
 * @param out_consumed receives the number of input bytes read, terminator
 *        included.  Either output may be NULL.
 * @return true when the end-of-stream opcode was reached without the output
 *         buffer overflowing.
 */
XXFC_API bool xx_lzvn_decompress_memory(const void *source,
                                        size_t source_size,
                                        void *destination,
                                        size_t destination_size,
                                        size_t *out_written,
                                        size_t *out_consumed);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_LZFSE_H */
