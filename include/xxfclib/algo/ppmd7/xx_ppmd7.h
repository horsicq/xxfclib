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
 * @file xx_ppmd7.h
 * @brief PPMd variant I rev.1 (PPMd7) codec.
 *
 * The property byte encodes order (bits 7..4) and memory (bits 3..0):
 *   order   = (prop >> 4) + 2   [2..17 for the ZIP property form]
 *   mem_mb  = (prop & 0xF) + 1  [1..16 MB]  (used as-is for PPMd I)
 */

#ifndef XX_PPMD7_H
#define XX_PPMD7_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum allowed memory for PPMd7 model (prevents OOM from crafted streams) */
#define XX_PPMD7_MAX_MEM_MB   256u
#define XX_PPMD7_MIN_MEM_SIZE (64u * 1024u)
#define XX_PPMD7_MAX_MEM_SIZE (XX_PPMD7_MAX_MEM_MB * 1024u * 1024u)

/* ========================================================================= */
/* --- PPMd7 Decompression                                                --- */
/* ========================================================================= */

/**
 * @brief Decompress a raw PPMd7 (variant I rev.1) stream.
 * @param order    PPMd order (2..64; 7-Zip commonly uses values above 16).
 * @param mem_mb   PPMd sub-allocator size in MB (1..256).
 */
XXFC_API bool xx_ppmd7_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                     int order, uint32_t mem_mb,
                                     xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_ppmd7_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                             int order, uint32_t mem_mb,
                                             const char *dst_file_path, xx_pd_struct *pd);

XXFC_API bool xx_ppmd7_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                               int order, uint32_t mem_mb,
                                               const wchar_t *dst_file_path_w, xx_pd_struct *pd);

XXFC_API bool xx_ppmd7_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                               int order, uint32_t mem_mb,
                                               void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                               xx_pd_struct *pd);

/**
 * @brief Decompress with an exact PPMd model allocation size in bytes.
 *
 * This form matches the five-byte 7-Zip PPMd property, whose memory value is
 * not restricted to whole MiB units.
 */
XXFC_API bool xx_ppmd7_unpack_device_to_memory_bytes(
    xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
    int order, uint32_t mem_size, void *dst_buf, size_t dst_buf_size,
    size_t *out_written, xx_pd_struct *pd);

XXFC_API bool xx_ppmd7_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                               int order, uint32_t mem_mb,
                                               xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_ppmd7_decompress_memory(const void *src_buf, size_t src_size,
                                         int order, uint32_t mem_mb,
                                         void *dst_buf, size_t dst_buf_size, size_t *out_written);

/* ========================================================================= */
/* --- PPMd7 Compression                                                  --- */
/* ========================================================================= */

/**
 * @brief Compress data from a device using PPMd7.
 * @param order    PPMd order (2..64, typically 6..8).
 * @param mem_mb   PPMd sub-allocator size in MB (1..256, typically 8..16).
 */
XXFC_API bool xx_ppmd7_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                   xx_io_device *dst_dev, int order, uint32_t mem_mb,
                                   xx_pd_struct *pd);

/**
 * @brief Pack an entire source device or file path to a destination device using PPMd7.
 */
XXFC_API bool xx_ppmd7_pack_source(xx_io_device *src_dev, const char *src_file_path,
                                   int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                                   xx_io_device *dst_dev, int order, uint32_t mem_mb,
                                   xx_pd_struct *pd);

/**
 * @brief Compress a contiguous memory buffer using PPMd7.
 */
XXFC_API bool xx_ppmd7_compress_memory(const void *src_buf, size_t src_size,
                                       void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                       int order, uint32_t mem_mb);

#ifdef __cplusplus
}
#endif

#endif /* XX_PPMD7_H */
