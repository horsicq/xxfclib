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
 * @file xx_data.h
 * @brief Binary data inspection and manipulation subsystem for raw buffers and xx_io_device objects.
 *
 * Supports reading, writing, and searching for 8, 16, 24, 32, 64-bit integers,
 * floating point values (f32, f64), ANSI strings, and Unicode strings in both
 * Little Endian and Big Endian byte order.
 */

#ifndef XX_DATA_H
#define XX_DATA_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Endianness boolean constants */
#define XX_LITTLE_ENDIAN false
#define XX_BIG_ENDIAN    true

/* ========================================================================= */
/* --- 1. Raw Memory Buffer Operations                                   --- */
/* ========================================================================= */

/* Reading types from raw buffer (safe, returns 0 if out of bounds) */
XXFC_API uint8_t  xx_data_get_u8(const void *data, size_t data_size, size_t offset);
XXFC_API int8_t   xx_data_get_i8(const void *data, size_t data_size, size_t offset);
XXFC_API uint16_t xx_data_get_u16(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API int16_t  xx_data_get_i16(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API uint32_t xx_data_get_u24(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API int32_t  xx_data_get_i24(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API uint32_t xx_data_get_u32(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API int32_t  xx_data_get_i32(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API uint64_t xx_data_get_u64(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API int64_t  xx_data_get_i64(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API float    xx_data_get_f32(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API double   xx_data_get_f64(const void *data, size_t data_size, size_t offset, bool big_endian);
/* IEEE-754 half, expanded to float. No host type holds a half, so this is
 * the only way to read one. */
XXFC_API float    xx_data_get_f16(const void *data, size_t data_size, size_t offset, bool big_endian);

/* Reading string from raw buffer (allocated via xx_mem_alloc, free with xx_str_free / xx_str_wfree) */
XXFC_API char*    xx_data_get_ansi_string(const void *data, size_t data_size, size_t offset, size_t max_len);
XXFC_API wchar_t* xx_data_get_unicode_string(const void *data, size_t data_size, size_t offset, size_t max_len, bool big_endian);

/* Writing types to raw buffer (returns false if out of bounds) */
XXFC_API bool xx_data_set_u8(void *data, size_t data_size, size_t offset, uint8_t val);
XXFC_API bool xx_data_set_i8(void *data, size_t data_size, size_t offset, int8_t val);
XXFC_API bool xx_data_set_u16(void *data, size_t data_size, size_t offset, uint16_t val, bool big_endian);
XXFC_API bool xx_data_set_i16(void *data, size_t data_size, size_t offset, int16_t val, bool big_endian);
XXFC_API bool xx_data_set_u24(void *data, size_t data_size, size_t offset, uint32_t val, bool big_endian);
XXFC_API bool xx_data_set_i24(void *data, size_t data_size, size_t offset, int32_t val, bool big_endian);
XXFC_API bool xx_data_set_u32(void *data, size_t data_size, size_t offset, uint32_t val, bool big_endian);
XXFC_API bool xx_data_set_i32(void *data, size_t data_size, size_t offset, int32_t val, bool big_endian);
XXFC_API bool xx_data_set_u64(void *data, size_t data_size, size_t offset, uint64_t val, bool big_endian);
XXFC_API bool xx_data_set_i64(void *data, size_t data_size, size_t offset, int64_t val, bool big_endian);
XXFC_API bool xx_data_set_f32(void *data, size_t data_size, size_t offset, float val, bool big_endian);
XXFC_API bool xx_data_set_f64(void *data, size_t data_size, size_t offset, double val, bool big_endian);
XXFC_API bool xx_data_set_bytes(void *data, size_t data_size, size_t offset, const void *src, size_t src_size);
XXFC_API bool xx_data_set_ansi_string(void *data, size_t data_size, size_t offset, const char *str);
XXFC_API bool xx_data_set_unicode_string(void *data, size_t data_size, size_t offset, const wchar_t *wstr, bool big_endian);

/* Finding types in raw buffer (returns byte offset in data or -1 if not found / cancelled) */
XXFC_API int64_t xx_data_find_bytes(const void *data, size_t data_size, size_t start_offset, const void *pattern, size_t pattern_size, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u8(const void *data, size_t data_size, size_t start_offset, uint8_t val, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u16(const void *data, size_t data_size, size_t start_offset, uint16_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u24(const void *data, size_t data_size, size_t start_offset, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u32(const void *data, size_t data_size, size_t start_offset, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u64(const void *data, size_t data_size, size_t start_offset, uint64_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_f32(const void *data, size_t data_size, size_t start_offset, float val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_f64(const void *data, size_t data_size, size_t start_offset, double val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_ansi_string(const void *data, size_t data_size, size_t start_offset, const char *str, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_unicode_string(const void *data, size_t data_size, size_t start_offset, const wchar_t *wstr, bool big_endian, xx_pd_struct *pd);

/* Optimized finding types in raw buffer (Sunday algorithm / SIMD / fast word scan) */
XXFC_API int64_t xx_data_find_bytes_buffer_optimize(const void *data, size_t data_size, size_t start_offset, const void *pattern, size_t pattern_size, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u8_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint8_t val, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u16_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint16_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u24_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u32_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_u64_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint64_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_f32_buffer_optimize(const void *data, size_t data_size, size_t start_offset, float val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_f64_buffer_optimize(const void *data, size_t data_size, size_t start_offset, double val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_ansi_string_buffer_optimize(const void *data, size_t data_size, size_t start_offset, const char *str, xx_pd_struct *pd);
XXFC_API int64_t xx_data_find_unicode_string_buffer_optimize(const void *data, size_t data_size, size_t start_offset, const wchar_t *wstr, bool big_endian, xx_pd_struct *pd);

/* ========================================================================= */
/* --- 2. xx_io_device Operations                                        --- */
/* ========================================================================= */

/* Reading types at specific offset in xx_io_device (seeks, reads, leaves file pos after read) */
XXFC_API uint8_t  xx_io_get_u8(xx_io_device *dev, int64_t offset);
XXFC_API int8_t   xx_io_get_i8(xx_io_device *dev, int64_t offset);
XXFC_API uint16_t xx_io_get_u16(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API int16_t  xx_io_get_i16(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API uint32_t xx_io_get_u24(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API int32_t  xx_io_get_i24(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API uint32_t xx_io_get_u32(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API int32_t  xx_io_get_i32(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API uint64_t xx_io_get_u64(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API int64_t  xx_io_get_i64(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API float    xx_io_get_f32(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API double   xx_io_get_f64(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API float    xx_io_get_f16(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API char*    xx_io_get_ansi_string(xx_io_device *dev, int64_t offset, size_t max_len);
XXFC_API wchar_t* xx_io_get_unicode_string(xx_io_device *dev, int64_t offset, size_t max_len, bool big_endian);

/* Sequential stream reading from current position of xx_io_device */
XXFC_API bool xx_io_read_u8(xx_io_device *dev, uint8_t *val);
XXFC_API bool xx_io_read_i8(xx_io_device *dev, int8_t *val);
XXFC_API bool xx_io_read_u16(xx_io_device *dev, uint16_t *val, bool big_endian);
XXFC_API bool xx_io_read_i16(xx_io_device *dev, int16_t *val, bool big_endian);
XXFC_API bool xx_io_read_u24(xx_io_device *dev, uint32_t *val, bool big_endian);
XXFC_API bool xx_io_read_i24(xx_io_device *dev, int32_t *val, bool big_endian);
XXFC_API bool xx_io_read_u32(xx_io_device *dev, uint32_t *val, bool big_endian);
XXFC_API bool xx_io_read_i32(xx_io_device *dev, int32_t *val, bool big_endian);
XXFC_API bool xx_io_read_u64(xx_io_device *dev, uint64_t *val, bool big_endian);
XXFC_API bool xx_io_read_i64(xx_io_device *dev, int64_t *val, bool big_endian);
XXFC_API bool xx_io_read_f32(xx_io_device *dev, float *val, bool big_endian);
XXFC_API bool xx_io_read_f64(xx_io_device *dev, double *val, bool big_endian);

/* Sequential stream writing to current position of xx_io_device */
XXFC_API bool xx_io_write_u8(xx_io_device *dev, uint8_t val);
XXFC_API bool xx_io_write_i8(xx_io_device *dev, int8_t val);
XXFC_API bool xx_io_write_u16(xx_io_device *dev, uint16_t val, bool big_endian);
XXFC_API bool xx_io_write_i16(xx_io_device *dev, int16_t val, bool big_endian);
XXFC_API bool xx_io_write_u24(xx_io_device *dev, uint32_t val, bool big_endian);
XXFC_API bool xx_io_write_i24(xx_io_device *dev, int32_t val, bool big_endian);
XXFC_API bool xx_io_write_u32(xx_io_device *dev, uint32_t val, bool big_endian);
XXFC_API bool xx_io_write_i32(xx_io_device *dev, int32_t val, bool big_endian);
XXFC_API bool xx_io_write_u64(xx_io_device *dev, uint64_t val, bool big_endian);
XXFC_API bool xx_io_write_i64(xx_io_device *dev, int64_t val, bool big_endian);
XXFC_API bool xx_io_write_f32(xx_io_device *dev, float val, bool big_endian);
XXFC_API bool xx_io_write_f64(xx_io_device *dev, double val, bool big_endian);
XXFC_API bool xx_io_write_ansi_string(xx_io_device *dev, const char *str);
XXFC_API bool xx_io_write_unicode_string(xx_io_device *dev, const wchar_t *wstr, bool big_endian);

/* Writing types at specific offset in xx_io_device */
XXFC_API bool xx_io_set_u8(xx_io_device *dev, int64_t offset, uint8_t val);
XXFC_API bool xx_io_set_i8(xx_io_device *dev, int64_t offset, int8_t val);
XXFC_API bool xx_io_set_u16(xx_io_device *dev, int64_t offset, uint16_t val, bool big_endian);
XXFC_API bool xx_io_set_i16(xx_io_device *dev, int64_t offset, int16_t val, bool big_endian);
XXFC_API bool xx_io_set_u24(xx_io_device *dev, int64_t offset, uint32_t val, bool big_endian);
XXFC_API bool xx_io_set_i24(xx_io_device *dev, int64_t offset, int32_t val, bool big_endian);
XXFC_API bool xx_io_set_u32(xx_io_device *dev, int64_t offset, uint32_t val, bool big_endian);
XXFC_API bool xx_io_set_i32(xx_io_device *dev, int64_t offset, int32_t val, bool big_endian);
XXFC_API bool xx_io_set_u64(xx_io_device *dev, int64_t offset, uint64_t val, bool big_endian);
XXFC_API bool xx_io_set_i64(xx_io_device *dev, int64_t offset, int64_t val, bool big_endian);
XXFC_API bool xx_io_set_f32(xx_io_device *dev, int64_t offset, float val, bool big_endian);
XXFC_API bool xx_io_set_f64(xx_io_device *dev, int64_t offset, double val, bool big_endian);
XXFC_API bool xx_io_set_ansi_string(xx_io_device *dev, int64_t offset, const char *str);
XXFC_API bool xx_io_set_unicode_string(xx_io_device *dev, int64_t offset, const wchar_t *wstr, bool big_endian);

/* Finding types in xx_io_device (returns file offset or -1 if not found / cancelled; max_search_len <= 0 means until EOF) */
XXFC_API int64_t xx_io_find_bytes(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u8(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint8_t val, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u16(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint16_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u24(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u32(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u64(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint64_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_f32(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, float val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_f64(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, double val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_ansi_string(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const char *str, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_unicode_string(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const wchar_t *wstr, bool big_endian, xx_pd_struct *pd);

/* Optimized finding types in xx_io_device using large streaming chunk buffer & fast search */
XXFC_API int64_t xx_io_find_bytes_buffer_optimize_ex(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size, size_t buffer_size, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_bytes_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u8_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint8_t val, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u16_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint16_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u24_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u32_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u64_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint64_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_f32_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, float val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_f64_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, double val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_ansi_string_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const char *str, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_unicode_string_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const wchar_t *wstr, bool big_endian, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_DATA_H */
