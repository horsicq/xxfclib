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
 * @file xx_bzip2.h
 * @brief Bzip2 (block-sorting) compression and decompression.
 *
 * Implements the Bzip2 format (ZIP compression method 12) using a clean C
 * rewrite of the BWT + MTF + Huffman core. Compression level maps to the
 * block-size multiplier: 1 = 100 kB blocks (fastest), 9 = 900 kB blocks.
 */

#ifndef XX_BZIP2_H
#define XX_BZIP2_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compression level constants (block-size multiplier * 100 kB) */
#define XX_BZIP2_LEVEL_FASTEST  1
#define XX_BZIP2_LEVEL_DEFAULT  6
#define XX_BZIP2_LEVEL_BEST     9

/* ========================================================================= */
/* --- Bzip2 Decompression                                                --- */
/* ========================================================================= */

XXFC_API bool xx_bzip2_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                     xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_bzip2_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                             const char *dst_file_path, xx_pd_struct *pd);

XXFC_API bool xx_bzip2_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                               const wchar_t *dst_file_path_w, xx_pd_struct *pd);

XXFC_API bool xx_bzip2_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                               void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                               xx_pd_struct *pd);

XXFC_API bool xx_bzip2_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                               xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_bzip2_decompress_memory(const void *src_buf, size_t src_size,
                                         void *dst_buf, size_t dst_buf_size, size_t *out_written);

/* ========================================================================= */
/* --- Bzip2 Compression                                                  --- */
/* ========================================================================= */

XXFC_API bool xx_bzip2_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                   xx_io_device *dst_dev, int level, xx_pd_struct *pd);

XXFC_API bool xx_bzip2_pack_source(xx_io_device *src_dev, const char *src_file_path,
                                   int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                                   xx_io_device *dst_dev, int level, xx_pd_struct *pd);

XXFC_API bool xx_bzip2_compress_memory(const void *src_buf, size_t src_size,
                                       void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                       int level);

#ifdef __cplusplus
}
#endif

#endif /* XX_BZIP2_H */
