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

#ifndef XX_ZSTD_H
#define XX_ZSTD_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Common Zstandard compression levels. */
#define XX_ZSTD_LEVEL_FASTEST 1
#define XX_ZSTD_LEVEL_DEFAULT 3
#define XX_ZSTD_LEVEL_BEST 22

/** Returns true because the native Zstandard implementation is built in. */
XXFC_API bool xx_zstd_is_available(void);

/**
 * Returns the maximum destination size needed by one-shot Zstandard
 * compression, or zero when the input size is unsupported.
 */
XXFC_API size_t xx_zstd_compress_bound(size_t source_size);

/**
 * Writes one standard Zstandard frame. The native baseline encoder uses raw
 * blocks, so output is interoperable but currently prioritizes independence
 * and bounded behavior over compression ratio.
 */
XXFC_API bool xx_zstd_compress_memory(const void *source, size_t source_size,
                                      void *destination, size_t destination_capacity,
                                      size_t *out_written, int level);

/** Decompresses complete standard Zstandard frames into an exact-size buffer. */
XXFC_API bool xx_zstd_decompress_memory(const void *source, size_t source_size,
                                        void *destination, size_t destination_size,
                                        size_t *out_written);

/** Compresses a fixed-size device range into one standard Zstandard frame. */
XXFC_API bool xx_zstd_pack_device(xx_io_device *source, int64_t source_offset,
                                  int64_t uncompressed_size, xx_io_device *destination,
                                  int level, xx_pd_struct *progress);

/**
 * Compresses a source device or file and reports its uncompressed size,
 * compressed size, and CRC-32. A supplied device takes precedence over the
 * source path.
 */
XXFC_API bool xx_zstd_pack_source(xx_io_device *source, const char *source_path,
                                  int64_t *out_uncompressed_size,
                                  int64_t *out_compressed_size,
                                  uint32_t *out_crc32, xx_io_device *destination,
                                  int level, xx_pd_struct *progress);

/** Decompresses one standard Zstandard frame into an output device. */
XXFC_API bool xx_zstd_unpack_device_to_device(xx_io_device *source, int64_t source_offset,
                                              int64_t compressed_size, xx_io_device *destination,
                                              uint64_t uncompressed_size, xx_pd_struct *progress);

/** Decompresses one standard Zstandard frame into a file. */
XXFC_API bool xx_zstd_unpack_device_to_file(xx_io_device *source, int64_t source_offset,
                                            int64_t compressed_size, const char *destination_path,
                                            uint64_t uncompressed_size, xx_pd_struct *progress);

#ifdef __cplusplus
}
#endif

#endif /* XX_ZSTD_H */
