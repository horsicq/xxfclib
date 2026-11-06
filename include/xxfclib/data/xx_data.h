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
#include <string.h>
#include <stdlib.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Endianness boolean constants */
#define XX_LITTLE_ENDIAN false
#define XX_BIG_ENDIAN true

/* ========================================================================= */
/* --- 0. Fast Unaligned Direct Memory Pointer Accessors                 --- */
/* ========================================================================= */

static inline uint16_t xx_data_get_le16(const void *p)
{
#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
#else
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
#endif
}

static inline void xx_data_set_le16(void *p, uint16_t v)
{
#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    memcpy(p, &v, sizeof(v));
#else
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
#endif
}

static inline uint16_t xx_data_get_be16(const void *p)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return _byteswap_ushort(v);
#elif defined(__GNUC__) || defined(__clang__)
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap16(v);
#else
    const uint8_t *b = (const uint8_t *)p;
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
#endif
}

static inline void xx_data_set_be16(void *p, uint16_t v)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    uint16_t swapped = _byteswap_ushort(v);
    memcpy(p, &swapped, sizeof(swapped));
#elif defined(__GNUC__) || defined(__clang__)
    uint16_t swapped = __builtin_bswap16(v);
    memcpy(p, &swapped, sizeof(swapped));
#else
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v >> 8);
    b[1] = (uint8_t)v;
#endif
}

static inline uint32_t xx_data_get_le32(const void *p)
{
#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
#else
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
#endif
}

static inline void xx_data_set_le32(void *p, uint32_t v)
{
#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    memcpy(p, &v, sizeof(v));
#else
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
#endif
}

static inline uint32_t xx_data_get_be32(const void *p)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return _byteswap_ulong(v);
#elif defined(__GNUC__) || defined(__clang__)
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap32(v);
#else
    const uint8_t *b = (const uint8_t *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
#endif
}

static inline void xx_data_set_be32(void *p, uint32_t v)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    uint32_t swapped = _byteswap_ulong(v);
    memcpy(p, &swapped, sizeof(swapped));
#elif defined(__GNUC__) || defined(__clang__)
    uint32_t swapped = __builtin_bswap32(v);
    memcpy(p, &swapped, sizeof(swapped));
#else
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
#endif
}

static inline uint64_t xx_data_get_le64(const void *p)
{
#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return v;
#else
    const uint8_t *b = (const uint8_t *)p;
    return (uint64_t)b[0] | ((uint64_t)b[1] << 8) | ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40) | ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
#endif
}

static inline void xx_data_set_le64(void *p, uint64_t v)
{
#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    memcpy(p, &v, sizeof(v));
#else
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    b[4] = (uint8_t)(v >> 32);
    b[5] = (uint8_t)(v >> 40);
    b[6] = (uint8_t)(v >> 48);
    b[7] = (uint8_t)(v >> 56);
#endif
}

static inline uint64_t xx_data_get_be64(const void *p)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return _byteswap_uint64(v);
#elif defined(__GNUC__) || defined(__clang__)
    uint64_t v;
    memcpy(&v, p, sizeof(v));
    return __builtin_bswap64(v);
#else
    const uint8_t *b = (const uint8_t *)p;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) | ((uint64_t)b[6] << 8) | (uint64_t)b[7];
#endif
}

static inline void xx_data_set_be64(void *p, uint64_t v)
{
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    uint64_t swapped = _byteswap_uint64(v);
    memcpy(p, &swapped, sizeof(swapped));
#elif defined(__GNUC__) || defined(__clang__)
    uint64_t swapped = __builtin_bswap64(v);
    memcpy(p, &swapped, sizeof(swapped));
#else
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v >> 56);
    b[1] = (uint8_t)(v >> 48);
    b[2] = (uint8_t)(v >> 40);
    b[3] = (uint8_t)(v >> 32);
    b[4] = (uint8_t)(v >> 24);
    b[5] = (uint8_t)(v >> 16);
    b[6] = (uint8_t)(v >> 8);
    b[7] = (uint8_t)v;
#endif
}

/* ========================================================================= */
/* --- 1. Raw Memory Buffer Operations                                   --- */
/* ========================================================================= */

/** Render bytes as lowercase hexadecimal, followed by a NUL terminator.
 * The output needs 2 * data_size + 1 bytes. Returns false for NULL pointers,
 * insufficient output space, or an unrepresentable output length. */
XXFC_API bool xx_data_to_hex(const void *data, size_t data_size, char *out, size_t out_size);

/* Reading types from raw buffer (safe, returns 0 if out of bounds) */
XXFC_API uint8_t xx_data_get_u8(const void *data, size_t data_size, size_t offset);
XXFC_API int8_t xx_data_get_i8(const void *data, size_t data_size, size_t offset);
XXFC_API uint16_t xx_data_get_u16(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API int16_t xx_data_get_i16(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API uint32_t xx_data_get_u24(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API int32_t xx_data_get_i24(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API uint32_t xx_data_get_u32(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API int32_t xx_data_get_i32(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API uint64_t xx_data_get_u64(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API int64_t xx_data_get_i64(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API float xx_data_get_f32(const void *data, size_t data_size, size_t offset, bool big_endian);
XXFC_API double xx_data_get_f64(const void *data, size_t data_size, size_t offset, bool big_endian);
/* IEEE-754 half, expanded to float. No host type holds a half, so this is
 * the only way to read one. */
XXFC_API float xx_data_get_f16(const void *data, size_t data_size, size_t offset, bool big_endian);

/* Reading string from raw buffer (allocated via xx_mem_alloc, free with xx_str_free / xx_str_wfree) */
XXFC_API char *xx_data_get_ansi_string(const void *data, size_t data_size, size_t offset, size_t max_len);
XXFC_API wchar_t *xx_data_get_unicode_string(const void *data, size_t data_size, size_t offset, size_t max_len, bool big_endian);

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
XXFC_API int64_t xx_data_find_unicode_string_buffer_optimize(const void *data, size_t data_size, size_t start_offset, const wchar_t *wstr, bool big_endian,
                                                             xx_pd_struct *pd);

/* ========================================================================= */
/* --- 2. xx_io_device Operations                                        --- */
/* ========================================================================= */

/* Reading types at specific offset in xx_io_device (seeks, reads, leaves file pos after read) */
XXFC_API uint8_t xx_io_get_u8(xx_io_device *dev, int64_t offset);
XXFC_API int8_t xx_io_get_i8(xx_io_device *dev, int64_t offset);
XXFC_API uint16_t xx_io_get_u16(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API int16_t xx_io_get_i16(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API uint32_t xx_io_get_u24(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API int32_t xx_io_get_i24(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API uint32_t xx_io_get_u32(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API int32_t xx_io_get_i32(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API uint64_t xx_io_get_u64(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API int64_t xx_io_get_i64(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API float xx_io_get_f32(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API double xx_io_get_f64(xx_io_device *dev, int64_t offset, bool big_endian);
XXFC_API float xx_io_get_f16(xx_io_device *dev, int64_t offset, bool big_endian);
/** Read a UUID as lowercase 8-4-4-4-12 hexadecimal groups. The first four
 * groups are little-endian integers (4,2,2,2 bytes); the final six bytes stay
 * in file order. Each failed scalar read contributes zero independently.
 * Reads reposition the device like xx_io_get_u32. Offsets above INT64_MAX-15
 * produce an all-zero UUID without I/O. Returns NULL on allocation failure;
 * otherwise free the allocated string with xx_str_free. */
XXFC_API char *xx_io_get_uuid(xx_io_device *dev, int64_t offset);
/** Read a region as uppercase hexadecimal, two characters per byte.
 * Negative size means the remaining bytes; oversized ranges stop at EOF.
 * Invalid/unknown-size devices, invalid offsets, and zero sizes return an
 * allocated empty string. A read failure returns the completed window prefix.
 * The device is borrowed. Buffered inputs keep their logical cursor; ordinary
 * inputs use a temporary buffer whose reads reposition the source.
 * Returns NULL on allocation failure or an unrepresentable output size.
 * Free the returned string with xx_str_free. */
XXFC_API char *xx_io_get_signature_hex(xx_io_device *dev, int64_t offset, int64_t size);
/** Test whether every byte in a nonempty device range is zero.
 * The entire range must lie within a known device size: invalid/empty ranges,
 * allocation failures, and read failures return false. The device is borrowed.
 * Buffered inputs keep their logical cursor; ordinary inputs use a temporary
 * buffer whose reads reposition the source. */
XXFC_API bool xx_io_is_zero_filled(xx_io_device *dev, int64_t offset, int64_t size);
XXFC_API char *xx_io_get_ansi_string(xx_io_device *dev, int64_t offset, size_t max_len);
XXFC_API wchar_t *xx_io_get_unicode_string(xx_io_device *dev, int64_t offset, size_t max_len, bool big_endian);

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

/* Search a clamped region of a known-size device. Negative size means the
 * remaining bytes; oversized sizes stop at EOF. Zero sizes, invalid offsets,
 * empty patterns/strings, read failures, and cancellation return -1.
 * Results are absolute file offsets. Devices are borrowed. Buffered inputs
 * retain their logical cursor and use resident/adaptive buffers; ordinary
 * inputs follow the streaming finder's cursor behavior. */
XXFC_API int64_t xx_io_find_bytes_in_range(xx_io_device *dev, int64_t offset, int64_t size, const void *pattern, size_t pattern_size, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_ansi_string_in_range(xx_io_device *dev, int64_t offset, int64_t size, const char *str, xx_pd_struct *pd);
/* Widen each unsigned input byte to one UTF-16 unit, without UTF-8 decoding. */
XXFC_API int64_t xx_io_find_unicode_bytes_in_range(xx_io_device *dev, int64_t offset, int64_t size, const char *str, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u8_in_range(xx_io_device *dev, int64_t offset, int64_t size, uint8_t val, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u16_in_range(xx_io_device *dev, int64_t offset, int64_t size, uint16_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u32_in_range(xx_io_device *dev, int64_t offset, int64_t size, uint32_t val, bool big_endian, xx_pd_struct *pd);

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
XXFC_API int64_t xx_io_find_bytes_buffer_optimize_ex(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size,
                                                     size_t buffer_size, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_bytes_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u8_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint8_t val, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u16_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint16_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u24_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u32_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_u64_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint64_t val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_f32_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, float val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_f64_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, double val, bool big_endian, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_ansi_string_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const char *str, xx_pd_struct *pd);
XXFC_API int64_t xx_io_find_unicode_string_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const wchar_t *wstr, bool big_endian,
                                                           xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XX_DATA_H */
