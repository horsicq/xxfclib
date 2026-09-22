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
 * @file xx_ppmd8.h
 * @brief PPMd variant H / PPMdI codec (used by ZIP method 98).
 *
 * Algorithm by Dmitry Shkarin (PPMd var.I, 2002) with Dmitry Subbotin's
 * carryless 32-bit range coder (1999).
 *
 * In ZIP archives (method 98), the stream begins with a 2-byte header:
 *   order          = (val & 0x0F) + 1        [2..16]
 *   mem_mb         = ((val >> 4) & 0xFF) + 1 [1..256 MB]
 *   restore_method = (val >> 12)             [0 = restart, 1 = cut off]
 */

#ifndef XX_PPMD8_H
#define XX_PPMD8_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XX_PPMD8_MIN_ORDER    2
#define XX_PPMD8_MAX_ORDER    16
#define XX_PPMD8_DEFAULT_ORDER 8

#define XX_PPMD8_MIN_MEM_MB   1u
#define XX_PPMD8_MAX_MEM_MB   256u
#define XX_PPMD8_DEFAULT_MEM_MB 16u

enum xx_ppmd8_restore_method {
    XX_PPMD8_RESTORE_METHOD_RESTART = 0,
    XX_PPMD8_RESTORE_METHOD_CUT_OFF = 1
};

/* ========================================================================= */
/* --- ZIP Properties Helpers                                             --- */
/* ========================================================================= */

/**
 * @brief Build 2-byte ZIP method 98 header word.
 */
XXFC_API uint16_t xx_ppmd8_build_zip_props(int order, uint32_t mem_mb, int restore_method);

/**
 * @brief Parse 2-byte ZIP method 98 header word.
 */
XXFC_API bool xx_ppmd8_parse_zip_props(uint16_t val, int *out_order, uint32_t *out_mem_mb, int *out_restore_method);

/* ========================================================================= */
/* --- PPMd8 Decompression                                                --- */
/* ========================================================================= */

/**
 * @brief Decompress a PPMd8 stream from a device to a destination device.
 * @param uncomp_size  Expected uncompressed size, or -1 if unknown (stop on EOF symbol).
 * @param order        Model order (2..16).
 * @param mem_mb       Sub-allocator size in MB (1..256).
 * @param restore_method 0 = restart, 1 = cut off.
 */
XXFC_API bool xx_ppmd8_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                     int64_t uncomp_size,
                                     int order, uint32_t mem_mb, int restore_method,
                                     xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                             int64_t uncomp_size,
                                             int order, uint32_t mem_mb, int restore_method,
                                             const char *dst_file_path, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                               int64_t uncomp_size,
                                               int order, uint32_t mem_mb, int restore_method,
                                               const wchar_t *dst_file_path_w, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, int64_t comp_size,
                                               int64_t uncomp_size,
                                               int order, uint32_t mem_mb, int restore_method,
                                               void *dst_buf, size_t dst_buf_size, size_t *out_written,
                                               xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_unpack_memory_to_device(const void *src_buf, size_t comp_size,
                                               int64_t uncomp_size,
                                               int order, uint32_t mem_mb, int restore_method,
                                               xx_io_device *dst_dev, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_decompress_memory(const void *src_buf, size_t src_size,
                                         int order, uint32_t mem_mb, int restore_method,
                                         void *dst_buf, size_t dst_buf_size, size_t *out_written);

/* ========================================================================= */
/* --- PPMd8 Compression                                                  --- */
/* ========================================================================= */

/**
 * @brief Compress data from a device using PPMd8.
 * @param write_zip_header If true, writes the 2-byte ZIP method 98 header before compressed data.
 */
XXFC_API bool xx_ppmd8_pack_device(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                   xx_io_device *dst_dev, int order, uint32_t mem_mb, int restore_method,
                                   bool write_zip_header, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_pack_source(xx_io_device *src_dev, const char *src_file_path,
                                   int64_t *out_uncomp_size, int64_t *out_comp_size, uint32_t *out_crc32,
                                   xx_io_device *dst_dev, int order, uint32_t mem_mb, int restore_method,
                                   bool write_zip_header, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_pack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                           const char *dst_file_path, int order, uint32_t mem_mb, int restore_method,
                                           bool write_zip_header, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_pack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t uncomp_size,
                                             const wchar_t *dst_file_path_w, int order, uint32_t mem_mb, int restore_method,
                                             bool write_zip_header, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_pack_memory_to_device(const void *src_buf, size_t uncomp_size,
                                             xx_io_device *dst_dev, int order, uint32_t mem_mb, int restore_method,
                                             bool write_zip_header, xx_pd_struct *pd);

XXFC_API bool xx_ppmd8_compress_memory(const void *src_buf, size_t src_size,
                                       int order, uint32_t mem_mb, int restore_method,
                                       bool write_zip_header,
                                       void *dst_buf, size_t dst_buf_size, size_t *out_written);

#ifdef __cplusplus
}
#endif

#endif /* XX_PPMD8_H */
