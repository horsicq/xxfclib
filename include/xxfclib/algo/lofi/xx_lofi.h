/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_LOFI_H
#define XXFCLIB_ALGO_LOFI_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode a Solaris compressed lofi disk image (`lofiadm -C lzma`).
 *
 * The container has no per-member framing -- the whole image is one logical
 * output -- so @p input must start at offset 0 of the file:
 *
 *     +0x00  char[36]   algorithm name, "lzma" followed by 32 zero bytes
 *     +0x24  u32 BE     segment size
 *     +0x28  u32 BE     number of index entries
 *     +0x2c  u32 BE     size of the final segment, 1..segment size
 *     +0x30  u64 BE[n]  index; entry i is the offset of segment i from the end
 *                       of the index, entry 0 is 0, and the LAST entry is the
 *                       end of the segment data rather than a segment start,
 *                       so there are n-1 segments.
 *
 * Each segment is [u8 0x01][13-byte LZMA "alone" header][LZMA data].  The
 * alone header carries the props, the dictionary size and the segment's
 * uncompressed length, which must equal the segment size for every segment but
 * the last.  Each segment is an independent LZMA stream decoded to its exact
 * declared length with no end marker (the reference calls LzmaDec_Init per
 * segment, i.e. state and dictionary reset at every segment boundary).
 * Nothing is checksummed.
 *
 * Only the "lzma" flavour is implemented; `lofiadm -C gzip` writes the same
 * container with a "gzip" name field and deflate segments, and the reference
 * refuses it outright rather than guessing at its framing, so this port does
 * too.
 *
 * @param input       The container, from offset 0.
 * @param input_size  Length of @p input.
 * @param output      Destination buffer.
 * @param output_size Capacity; must be at least the image size the header
 *                    describes.  Exactly the image size is produced.
 * @param written     Receives the produced byte count (0 on failure).
 * @return true only on a complete decode of the whole image.
 */
XXFC_API bool xx_lofi_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);

/**
 * @brief Report the image size and container extent without decoding.
 *
 * The plaintext length lives in the container's own header, so a reader that
 * has not parsed it can size its buffer with this.  It runs exactly the header
 * and index validation @ref xx_lofi_decode_memory runs; it does NOT run the
 * LZMA streams, so a stream that is itself corrupt is reported here and only
 * fails at decode time.  @p produced is the image size, @p consumed the number
 * of container bytes the index accounts for (index end + last index entry).
 * Fails if the image would exceed @p max_output.
 */
XXFC_API bool xx_lofi_scan_memory(const uint8_t *input, size_t input_size,
                                  size_t max_output, size_t *consumed,
                                  size_t *produced);

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_ALGO_LOFI_H */
