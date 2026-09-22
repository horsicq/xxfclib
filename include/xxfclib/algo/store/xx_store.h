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
 * @file xx_store.h
 * @brief Functions for STORE (uncompressed stream) packing, unpacking, and extraction.
 */

#ifndef XX_STORE_H
#define XX_STORE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* --- STORE Unpacking / Extraction                                      --- */
/* ========================================================================= */

/**
 * @brief Unpack a STORE stream from a source I/O device to a destination I/O device.
 * @param src_dev Source I/O device.
 * @param src_offset Start byte offset in src_dev (-1 to use current seek position).
 * @param size Number of bytes to unpack / transfer.
 * @param dst_dev Destination I/O device.
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on error or user cancellation.
 */
XXFC_API bool xx_store_unpack_device(xx_io_device *src_dev, int64_t src_offset, int64_t size,
                                    xx_io_device *dst_dev, xx_pd_struct *pd);

/**
 * @brief Unpack a STORE stream from a source I/O device directly into a destination file on disk.
 * @param src_dev Source I/O device.
 * @param src_offset Start byte offset in src_dev (-1 to use current seek position).
 * @param size Number of bytes to unpack / transfer.
 * @param dst_file_path UTF-8 destination file path.
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on error.
 */
XXFC_API bool xx_store_unpack_device_to_file(xx_io_device *src_dev, int64_t src_offset, int64_t size,
                                            const char *dst_file_path, xx_pd_struct *pd);

/**
 * @brief Unpack a STORE stream from a source I/O device directly into a destination file on disk (wchar_t path).
 */
XXFC_API bool xx_store_unpack_device_to_file_w(xx_io_device *src_dev, int64_t src_offset, int64_t size,
                                              const wchar_t *dst_file_path_w, xx_pd_struct *pd);

/**
 * @brief Unpack a STORE stream from memory buffer to a destination I/O device.
 * @param src_buf Source memory buffer.
 * @param size Number of bytes to unpack / transfer.
 * @param dst_dev Destination I/O device.
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on error.
 */
XXFC_API bool xx_store_unpack_memory_to_device(const void *src_buf, size_t size,
                                              xx_io_device *dst_dev, xx_pd_struct *pd);

/**
 * @brief Unpack a STORE stream from a source I/O device into a destination memory buffer.
 * @param src_dev Source I/O device.
 * @param src_offset Start byte offset in src_dev (-1 to use current seek position).
 * @param size Number of bytes to unpack.
 * @param dst_buf Destination memory buffer.
 * @param dst_buf_size Capacity of destination buffer in bytes.
 * @param out_written Optional output pointer to receive total bytes transferred.
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on error or buffer overflow.
 */
XXFC_API bool xx_store_unpack_device_to_memory(xx_io_device *src_dev, int64_t src_offset, size_t size,
                                              void *dst_buf, size_t dst_buf_size,
                                              size_t *out_written, xx_pd_struct *pd);


/* ========================================================================= */
/* --- Directory Creation for Extraction                                 --- */
/* ========================================================================= */

/**
 * @brief Recursively create parent directories or folder path (wide string).
 */
XXFC_API bool xx_store_create_dirs_w(const wchar_t *path, bool is_dir);

/**
 * @brief Recursively create parent directories or folder path (ANSI / UTF-8 string).
 */
XXFC_API bool xx_store_create_dirs_a(const char *path, bool is_dir);

/* ========================================================================= */
/* --- File Timestamp and Attribute Application                          --- */
/* ========================================================================= */

/**
 * @brief Apply DOS date, time, and external attributes to an extracted file on disk (wchar_t path).
 * @param path Wide path of the file or directory.
 * @param dos_date DOS date (bits: 0-4 day, 5-8 month, 9-15 year-1980). 0 to skip time.
 * @param dos_time DOS time (bits: 0-4 sec/2, 5-10 min, 11-15 hour). 0 to skip time.
 * @param attrs External/DOS attributes (e.g. 0x01 Read-Only, 0x10 Directory, 0x20 Archive). 0 to skip.
 * @return True on success.
 */
XXFC_API bool xx_store_apply_dos_time_and_attrs_w(const wchar_t *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs);

/**
 * @brief Apply DOS date, time, and external attributes to an extracted file on disk (UTF-8 path).
 */
XXFC_API bool xx_store_apply_dos_time_and_attrs_a(const char *path, uint16_t dos_date, uint16_t dos_time, uint32_t attrs);

/* ========================================================================= */
/* --- STORE Packing                                                     --- */
/* ========================================================================= */

/**
 * @brief Inspect an input source (open device or file path on disk) to obtain
 * its total size and compute its CRC-32 checksum without loading into memory.
 * Resets the device seek position back to 0 on completion.
 * @param src_dev Open source I/O device (or NULL to open src_file_path).
 * @param src_file_path UTF-8 path to open if src_dev is NULL.
 * @param out_size Output pointer for size in bytes.
 * @param out_crc32 Output pointer for calculated CRC-32.
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on error.
 */
XXFC_API bool xx_store_prepare_source(xx_io_device *src_dev, const char *src_file_path,
                                     int64_t *out_size, uint32_t *out_crc32, xx_pd_struct *pd);

/**
 * @brief Pack STORE stream: transfers `size` bytes from `src_dev` (or `src_file_path` if src_dev is NULL)
 * directly into `dst_dev` at current seek position using chunked streaming.
 * @param src_dev Open source I/O device (or NULL to open src_file_path).
 * @param src_file_path UTF-8 path to open if src_dev is NULL.
 * @param size Number of bytes to transfer.
 * @param dst_dev Destination I/O device.
 * @param pd Optional progress and cancellation monitor.
 * @return True on success, false on error.
 */
XXFC_API bool xx_store_pack_source(xx_io_device *src_dev, const char *src_file_path,
                                  int64_t size, xx_io_device *dst_dev, xx_pd_struct *pd);

/* ========================================================================= */
/* --- Convenience Aliases                                               --- */
/* ========================================================================= */

static inline bool XStore_unpack_device(xx_io_device *src, int64_t offset, int64_t size,
                                        xx_io_device *dst, xx_pd_struct *pd) {
    return xx_store_unpack_device(src, offset, size, dst, pd);
}

static inline bool XStore_unpack_device_to_file(xx_io_device *src, int64_t offset, int64_t size,
                                               const char *path, xx_pd_struct *pd) {
    return xx_store_unpack_device_to_file(src, offset, size, path, pd);
}

#ifdef __cplusplus
}
#endif

#endif /* XX_STORE_H */
