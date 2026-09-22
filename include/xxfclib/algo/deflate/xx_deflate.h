/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_deflate.h
 * @brief Deflate (RFC 1951) and Deflate64 (Enhanced Deflate) compression and decompression.
 */

#ifndef XX_DEFLATE_H
#define XX_DEFLATE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compression levels */
#define XX_DEFLATE_LEVEL_STORED   0
#define XX_DEFLATE_LEVEL_FASTEST  1
#define XX_DEFLATE_LEVEL_DEFAULT  6
#define XX_DEFLATE_LEVEL_BEST     9

/* ========================================================================= */
/* --- Deflate / Deflate64 Decompression (Unpacking)                     --- */
/* ========================================================================= */

/**
 * @brief Unpack a raw Deflate or Deflate64 stream from src_dev to dst_dev.
 * @param src_dev Source I/O device containing compressed data.
 * @param src_offset Start byte offset in src_dev (-1 to use current seek position).
 * @param comp_size Compressed stream size in bytes (-1 if unbounded until stream end).
 * @param dst_dev Destination I/O device receiving decompressed data.
 * @param is_deflate64 True for Deflate64 (64KB window), false for standard Deflate (32KB window).
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on format error or cancellation.
 */
XXFC_API bool xx_deflate_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                       xx_io_device *dst_dev, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Unpack a Deflate / Deflate64 stream directly to a disk file (UTF-8 path).
 */
XXFC_API bool xx_deflate_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                              const char *dst_file_path, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Unpack a Deflate / Deflate64 stream directly to a disk file (wchar_t path).
 */
XXFC_API bool xx_deflate_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                                const wchar_t *dst_file_path_w, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Unpack a Deflate / Deflate64 stream to a memory buffer.
 */
XXFC_API bool xx_deflate_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                                void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                                bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Unpack a Deflate / Deflate64 stream from memory to an I/O device.
 */
XXFC_API bool xx_deflate_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                                xx_io_device *dst_dev, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Decode one raw Deflate stream and report the number of source bytes
 * consumed through its final block.  Bytes after the stream are left for the
 * caller, which is needed by container formats such as zlib.
 */
XXFC_API bool xx_deflate_unpack_memory_to_device_ex(
    const void *src_buf, size_t comp_size, xx_io_device *dst_dev,
    size_t *out_consumed, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Pure in-memory decompression of a Deflate or Deflate64 buffer.
 */
XXFC_API bool xx_deflate_decompress_memory(const void *src_buf, size_t src_size,
                                          void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                          bool is_deflate64);

/* ========================================================================= */
/* --- Deflate / Deflate64 Compression (Packing)                         --- */
/* ========================================================================= */

/**
 * @brief Compress data from src_dev into dst_dev using Deflate or Deflate64.
 * @param src_dev Source I/O device with uncompressed data.
 * @param src_offset Start byte offset in src_dev (-1 to use current seek position).
 * @param uncomp_size Number of bytes to read and compress (-1 for all remaining).
 * @param dst_dev Destination device receiving the raw compressed stream.
 * @param level Compression level (0 = stored, 1 = fastest, 6 = default, 9 = best).
 * @param is_deflate64 True to produce Deflate64 (64KB window), false for Deflate (32KB).
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on error or cancellation.
 */
XXFC_API bool xx_deflate_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                     xx_io_device *dst_dev, int level, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Inspect an input source, compute its uncompressed size and CRC32, and compress
 * into dst_dev. Resets src_dev seek position upon completion.
 */
XXFC_API bool xx_deflate_pack_source(xx_io_device *src_dev, const char *src_file_path,
                                     int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                                     xx_io_device *dst_dev, int level, bool is_deflate64, xx_pd_struct *pd);

/**
 * @brief Pure in-memory compression of a buffer using Deflate or Deflate64.
 */
XXFC_API bool xx_deflate_compress_memory(const void *src_buf, size_t src_size,
                                        void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                        int level, bool is_deflate64);

/* ========================================================================= */
/* --- zlib stream wrapper (RFC 1950)                                    --- */
/* ========================================================================= */

/**
 * @brief Whether @p input opens with a well-formed zlib header.
 *
 * Checks the method, the window size, the header's multiple-of-31 rule and
 * the preset-dictionary flag. A stream requesting a preset dictionary is
 * reported invalid: the dictionary is not in the stream, so it cannot be
 * decoded here.
 */
XXFC_API bool xx_zlib_stream_header_is_valid(const uint8_t *input,
                                             size_t input_size);

/**
 * @brief Decode a zlib stream: header check, then raw Deflate.
 *
 * The trailing Adler-32 is not required to be present, because a container
 * that records a member's exact compressed length may cut the stream at its
 * last Deflate byte. Use xx_zlib_stream_trailer_matches() where it is.
 */
XXFC_API bool xx_zlib_stream_decode_memory(const uint8_t *input,
                                           size_t input_size, uint8_t *output,
                                           size_t output_size,
                                           size_t *written);

/** @brief Adler-32 of @p data, as zlib defines it. */
XXFC_API uint32_t xx_zlib_stream_adler32(const uint8_t *data, size_t size);

/**
 * @brief Whether the zlib trailer in @p input matches @p plain.
 *
 * False when the stream is too short to carry a trailer at all, so a caller
 * must only use this where the container guarantees the full stream.
 */
XXFC_API bool xx_zlib_stream_trailer_matches(const uint8_t *input,
                                             size_t input_size,
                                             const uint8_t *plain,
                                             size_t plain_size);

#ifdef __cplusplus
}
#endif

#endif /* XX_DEFLATE_H */
