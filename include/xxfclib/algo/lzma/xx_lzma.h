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
 * @file xx_lzma.h
 * @brief LZMA (ZIP method 14) and LZMA2 (ZIP method 33) compression and decompression.
 *
 * LZMA property bytes (5 bytes for LZMA, 1 byte for LZMA2) are provided
 * separately by the caller. ZIP stores them in a prefix at the start of the
 * compressed entry payload (4-byte version/size prefix followed by properties).
 */

#ifndef XX_LZMA_H
#define XX_LZMA_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LZMA property structure: lc | lp | pb encoded in 5 bytes */
#define XX_LZMA_PROPS_SIZE   5
#define XX_LZMA2_PROPS_SIZE  1

/* Compression level constants */
#define XX_LZMA_LEVEL_FASTEST  1
#define XX_LZMA_LEVEL_DEFAULT  5
#define XX_LZMA_LEVEL_BEST     9

/* Maximum dictionary size accepted (prevents OOM from crafted streams) */
#define XX_LZMA_MAX_DICT_SIZE  (512u * 1024u * 1024u)

/* ========================================================================= */
/* --- LZMA Decompression                                                 --- */
/* ========================================================================= */

/**
 * @brief Decompress a raw LZMA stream.
 * @param props      5-byte LZMA property block (lc/lp/pb + 4-byte dict size).
 * @param props_size Must be XX_LZMA_PROPS_SIZE (5).
 * @param uncomp_size Expected uncompressed size (-1 if unknown / end-of-stream signalled).
 */
XXFC_API bool xx_lzma_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                    const uint8_t *props, size_t props_size,
                                    int64_t uncomp_size,
                                    xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_lzma_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                            const uint8_t *props, size_t props_size,
                                            int64_t uncomp_size,
                                            const char *dst_file_path, xx_pd_struct *pd);

XXFC_API bool xx_lzma_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                              const uint8_t *props, size_t props_size,
                                              int64_t uncomp_size,
                                              const wchar_t *dst_file_path_w, xx_pd_struct *pd);

XXFC_API bool xx_lzma_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                              const uint8_t *props, size_t props_size,
                                              int64_t uncomp_size,
                                              void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                              xx_pd_struct *pd);

XXFC_API bool xx_lzma_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                              const uint8_t *props, size_t props_size,
                                              int64_t uncomp_size,
                                              xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_lzma_decompress_memory(const void *src_buf, size_t src_size,
                                        const uint8_t *props, size_t props_size,
                                        int64_t uncomp_size,
                                        void *dst_buf, size_t dst_buf_size, size_t *out_written);

/* ========================================================================= */
/* --- LZMA2 Decompression                                                --- */
/* ========================================================================= */

/**
 * @brief Decompress a raw LZMA2 stream.
 * @param props2_byte  Single LZMA2 property byte (dict-size exponent).
 */
XXFC_API bool xx_lzma2_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                     uint8_t props2_byte,
                                     xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_lzma2_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                             uint8_t props2_byte,
                                             const char *dst_file_path, xx_pd_struct *pd);

XXFC_API bool xx_lzma2_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                               uint8_t props2_byte,
                                               const wchar_t *dst_file_path_w, xx_pd_struct *pd);

XXFC_API bool xx_lzma2_decompress_memory(const void *src_buf, size_t src_size,
                                         uint8_t props2_byte,
                                         void *dst_buf, size_t dst_buf_size, size_t *out_written);

/* ========================================================================= */
/* --- LZMA2 Compression                                                   --- */
/* ========================================================================= */

/**
 * @brief Compress data as a raw LZMA2 stream.
 * @param out_props2_byte Receives the single LZMA2 dictionary property byte.
 */
XXFC_API bool xx_lzma2_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                   xx_io_device *dst_dev, int level,
                                   uint8_t *out_props2_byte, xx_pd_struct *pd);

XXFC_API bool xx_lzma2_pack_source(xx_io_device *src_dev, const char *src_file_path,
                                   int64_t *out_uncomp_size, int64_t *out_comp_size,
                                   uint32_t *out_crc32, xx_io_device *dst_dev, int level,
                                   uint8_t *out_props2_byte, xx_pd_struct *pd);

XXFC_API bool xx_lzma2_compress_memory(const void *src_buf, size_t src_size,
                                       void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                       int level, uint8_t *out_props2_byte);

/* ========================================================================= */
/* --- LZMA Compression                                                   --- */
/* ========================================================================= */

/**
 * @brief Compress data using LZMA.
 * @param out_props      Receives 5-byte property block (caller must provide 5-byte buffer).
 * @param out_props_size Receives XX_LZMA_PROPS_SIZE on success.
 */
XXFC_API bool xx_lzma_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                  xx_io_device *dst_dev, int level,
                                  uint8_t *out_props, size_t *out_props_size,
                                  xx_pd_struct *pd);

XXFC_API bool xx_lzma_pack_source(xx_io_device *src_dev, const char *src_file_path,
                                  int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                                  xx_io_device *dst_dev, int level,
                                  uint8_t *out_props, size_t *out_props_size,
                                  xx_pd_struct *pd);

XXFC_API bool xx_lzma_compress_memory(const void *src_buf, size_t src_size,
                                      void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                      int level,
                                      uint8_t *out_props, size_t *out_props_size);

#ifdef __cplusplus
}
#endif

#endif /* XX_LZMA_H */
