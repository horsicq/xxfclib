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
 * @file xx_data_io.c
 * @brief xx_io_device binary data reading, writing, and search implementation.
 */

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/global/xx_global.h"

/* ========================================================================= */
/* --- Internal Helpers                                                  --- */
/* ========================================================================= */

static size_t xx_io_captured_capacity(void) {
    size_t capacity = xx_get_file_buffer_size();
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (capacity > (SIZE_MAX >> 1)) capacity = SIZE_MAX >> 1;
    return capacity;
}

static bool xx_io_read_exact_capped(xx_io_device *dev, void *buf, size_t n, size_t capacity) {
    if (!dev || !buf || n == 0) {
        return false;
    }
    uint8_t *p = (uint8_t*)buf;
    size_t total = 0;
    while (total < n) {
        size_t request = n - total;
        if (request > capacity) request = capacity;
        ssize_t r = xx_io_read(dev, p + total, request);
        if (r <= 0 || (size_t)r > request) {
            return false;
        }
        total += (size_t)r;
    }
    return true;
}

static bool xx_io_read_exact(xx_io_device *dev, void *buf, size_t n) {
    return xx_io_read_exact_capped(dev, buf, n, xx_io_captured_capacity());
}

static bool xx_io_write_exact(xx_io_device *dev, const void *buf, size_t n) {
    if (!dev || !buf || n == 0) {
        return false;
    }
    const uint8_t *p = (const uint8_t*)buf;
    size_t capacity = xx_io_captured_capacity();
    size_t total = 0;
    while (total < n) {
        size_t request = n - total;
        if (request > capacity) request = capacity;
        ssize_t w = xx_io_write(dev, p + total, request);
        if (w <= 0 || (size_t)w > request) {
            return false;
        }
        total += (size_t)w;
    }
    return true;
}

/* ========================================================================= */
/* --- Reading from xx_io_device at offset                               --- */
/* ========================================================================= */

uint8_t xx_io_get_u8(xx_io_device *dev, int64_t offset) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return 0;
    }
    uint8_t b = 0;
    if (!xx_io_read_exact(dev, &b, 1)) {
        return 0;
    }
    return b;
}

int8_t xx_io_get_i8(xx_io_device *dev, int64_t offset) {
    return (int8_t)xx_io_get_u8(dev, offset);
}

uint16_t xx_io_get_u16(xx_io_device *dev, int64_t offset, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return 0;
    }
    uint8_t buf[2];
    if (!xx_io_read_exact(dev, buf, 2)) {
        return 0;
    }
    return xx_data_get_u16(buf, 2, 0, big_endian);
}

int16_t xx_io_get_i16(xx_io_device *dev, int64_t offset, bool big_endian) {
    return (int16_t)xx_io_get_u16(dev, offset, big_endian);
}

uint32_t xx_io_get_u24(xx_io_device *dev, int64_t offset, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return 0;
    }
    uint8_t buf[3];
    if (!xx_io_read_exact(dev, buf, 3)) {
        return 0;
    }
    return xx_data_get_u24(buf, 3, 0, big_endian);
}

int32_t xx_io_get_i24(xx_io_device *dev, int64_t offset, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return 0;
    }
    uint8_t buf[3];
    if (!xx_io_read_exact(dev, buf, 3)) {
        return 0;
    }
    return xx_data_get_i24(buf, 3, 0, big_endian);
}

uint32_t xx_io_get_u32(xx_io_device *dev, int64_t offset, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return 0;
    }
    uint8_t buf[4];
    if (!xx_io_read_exact(dev, buf, 4)) {
        return 0;
    }
    return xx_data_get_u32(buf, 4, 0, big_endian);
}

int32_t xx_io_get_i32(xx_io_device *dev, int64_t offset, bool big_endian) {
    return (int32_t)xx_io_get_u32(dev, offset, big_endian);
}

uint64_t xx_io_get_u64(xx_io_device *dev, int64_t offset, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return 0;
    }
    uint8_t buf[8];
    if (!xx_io_read_exact(dev, buf, 8)) {
        return 0;
    }
    return xx_data_get_u64(buf, 8, 0, big_endian);
}

int64_t xx_io_get_i64(xx_io_device *dev, int64_t offset, bool big_endian) {
    return (int64_t)xx_io_get_u64(dev, offset, big_endian);
}

float xx_io_get_f32(xx_io_device *dev, int64_t offset, bool big_endian) {
    uint32_t u = xx_io_get_u32(dev, offset, big_endian);
    float f = 0.0f;
    xx_mem_copy(&f, &u, sizeof(float));
    return f;
}

float xx_io_get_f16(xx_io_device *dev, int64_t offset, bool big_endian) {
    /* The half-to-single expansion lives in xx_data_get_f16; read the two
     * bytes here and let the buffer side do the arithmetic once. */
    uint8_t buf[2];
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return 0.0f;
    }
    if (!xx_io_read_exact(dev, buf, 2)) {
        return 0.0f;
    }
    return xx_data_get_f16(buf, 2, 0, big_endian);
}

double xx_io_get_f64(xx_io_device *dev, int64_t offset, bool big_endian) {
    uint64_t u = xx_io_get_u64(dev, offset, big_endian);
    double d = 0.0;
    xx_mem_copy(&d, &u, sizeof(double));
    return d;
}

char* xx_io_get_ansi_string(xx_io_device *dev, int64_t offset, size_t max_len) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return NULL;
    }
    size_t default_buf = xx_get_buffer_size();
    if (default_buf < 256) {
        default_buf = 256;
    }
    size_t cap = (max_len > 0 && max_len < default_buf) ? max_len + 1 : default_buf;
    char *res = (char*)xx_mem_alloc(cap);
    if (!res) {
        return NULL;
    }

    size_t len = 0;
    while (max_len == 0 || len < max_len) {
        char ch = 0;
        if (xx_io_read(dev, &ch, 1) != 1 || ch == '\0') {
            break;
        }
        if (len + 1 >= cap) {
            cap = cap + (cap >> 1);
            char *new_res = (char*)xx_mem_realloc(res, cap);
            if (!new_res) {
                xx_mem_free(res);
                return NULL;
            }
            res = new_res;
        }
        res[len++] = ch;
    }
    res[len] = '\0';
    return res;
}

wchar_t* xx_io_get_unicode_string(xx_io_device *dev, int64_t offset, size_t max_len, bool big_endian) {
    size_t transfer_capacity = xx_io_captured_capacity();
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return NULL;
    }
    size_t default_buf = xx_get_buffer_size();
    if (default_buf < 256) {
        default_buf = 256;
    }
    size_t cap = (max_len > 0 && max_len < default_buf) ? max_len + 1 : default_buf;
    wchar_t *res = (wchar_t*)xx_mem_alloc(cap * sizeof(wchar_t));
    if (!res) {
        return NULL;
    }

    size_t units = 0;
    while (max_len == 0 || units < max_len) {
        uint8_t buf[2];
        if (!xx_io_read_exact_capped(dev, buf, 2, transfer_capacity)) {
            break;
        }
        uint16_t ch = xx_data_get_u16(buf, 2, 0, big_endian);
        if (ch == 0) {
            break;
        }
        if (units + 1 >= cap) {
            cap = cap + (cap >> 1);
            wchar_t *new_res = (wchar_t*)xx_mem_realloc(res, cap * sizeof(wchar_t));
            if (!new_res) {
                xx_mem_free(res);
                return NULL;
            }
            res = new_res;
        }
        res[units++] = (wchar_t)ch;
    }
    res[units] = L'\0';
    return res;
}

/* ========================================================================= */
/* --- Sequential Reading from xx_io_device                              --- */
/* ========================================================================= */

bool xx_io_read_u8(xx_io_device *dev, uint8_t *val) {
    return dev && val && xx_io_read_exact(dev, val, 1);
}

bool xx_io_read_i8(xx_io_device *dev, int8_t *val) {
    return dev && val && xx_io_read_exact(dev, val, 1);
}

bool xx_io_read_u16(xx_io_device *dev, uint16_t *val, bool big_endian) {
    if (!dev || !val) {
        return false;
    }
    uint8_t buf[2];
    if (!xx_io_read_exact(dev, buf, 2)) {
        return false;
    }
    *val = xx_data_get_u16(buf, 2, 0, big_endian);
    return true;
}

bool xx_io_read_i16(xx_io_device *dev, int16_t *val, bool big_endian) {
    return xx_io_read_u16(dev, (uint16_t*)val, big_endian);
}

bool xx_io_read_u24(xx_io_device *dev, uint32_t *val, bool big_endian) {
    if (!dev || !val) {
        return false;
    }
    uint8_t buf[3];
    if (!xx_io_read_exact(dev, buf, 3)) {
        return false;
    }
    *val = xx_data_get_u24(buf, 3, 0, big_endian);
    return true;
}

bool xx_io_read_i24(xx_io_device *dev, int32_t *val, bool big_endian) {
    if (!dev || !val) {
        return false;
    }
    uint8_t buf[3];
    if (!xx_io_read_exact(dev, buf, 3)) {
        return false;
    }
    *val = xx_data_get_i24(buf, 3, 0, big_endian);
    return true;
}

bool xx_io_read_u32(xx_io_device *dev, uint32_t *val, bool big_endian) {
    if (!dev || !val) {
        return false;
    }
    uint8_t buf[4];
    if (!xx_io_read_exact(dev, buf, 4)) {
        return false;
    }
    *val = xx_data_get_u32(buf, 4, 0, big_endian);
    return true;
}

bool xx_io_read_i32(xx_io_device *dev, int32_t *val, bool big_endian) {
    return xx_io_read_u32(dev, (uint32_t*)val, big_endian);
}

bool xx_io_read_u64(xx_io_device *dev, uint64_t *val, bool big_endian) {
    if (!dev || !val) {
        return false;
    }
    uint8_t buf[8];
    if (!xx_io_read_exact(dev, buf, 8)) {
        return false;
    }
    *val = xx_data_get_u64(buf, 8, 0, big_endian);
    return true;
}

bool xx_io_read_i64(xx_io_device *dev, int64_t *val, bool big_endian) {
    return xx_io_read_u64(dev, (uint64_t*)val, big_endian);
}

bool xx_io_read_f32(xx_io_device *dev, float *val, bool big_endian) {
    if (!dev || !val) {
        return false;
    }
    uint32_t u = 0;
    if (!xx_io_read_u32(dev, &u, big_endian)) {
        return false;
    }
    xx_mem_copy(val, &u, sizeof(float));
    return true;
}

bool xx_io_read_f64(xx_io_device *dev, double *val, bool big_endian) {
    if (!dev || !val) {
        return false;
    }
    uint64_t u = 0;
    if (!xx_io_read_u64(dev, &u, big_endian)) {
        return false;
    }
    xx_mem_copy(val, &u, sizeof(double));
    return true;
}

/* ========================================================================= */
/* --- Sequential Writing to xx_io_device                                --- */
/* ========================================================================= */

bool xx_io_write_u8(xx_io_device *dev, uint8_t val) {
    return dev && xx_io_write_exact(dev, &val, 1);
}

bool xx_io_write_i8(xx_io_device *dev, int8_t val) {
    return xx_io_write_u8(dev, (uint8_t)val);
}

bool xx_io_write_u16(xx_io_device *dev, uint16_t val, bool big_endian) {
    uint8_t buf[2];
    xx_data_set_u16(buf, 2, 0, val, big_endian);
    return dev && xx_io_write_exact(dev, buf, 2);
}

bool xx_io_write_i16(xx_io_device *dev, int16_t val, bool big_endian) {
    return xx_io_write_u16(dev, (uint16_t)val, big_endian);
}

bool xx_io_write_u24(xx_io_device *dev, uint32_t val, bool big_endian) {
    uint8_t buf[3];
    xx_data_set_u24(buf, 3, 0, val, big_endian);
    return dev && xx_io_write_exact(dev, buf, 3);
}

bool xx_io_write_i24(xx_io_device *dev, int32_t val, bool big_endian) {
    return xx_io_write_u24(dev, (uint32_t)val, big_endian);
}

bool xx_io_write_u32(xx_io_device *dev, uint32_t val, bool big_endian) {
    uint8_t buf[4];
    xx_data_set_u32(buf, 4, 0, val, big_endian);
    return dev && xx_io_write_exact(dev, buf, 4);
}

bool xx_io_write_i32(xx_io_device *dev, int32_t val, bool big_endian) {
    return xx_io_write_u32(dev, (uint32_t)val, big_endian);
}

bool xx_io_write_u64(xx_io_device *dev, uint64_t val, bool big_endian) {
    uint8_t buf[8];
    xx_data_set_u64(buf, 8, 0, val, big_endian);
    return dev && xx_io_write_exact(dev, buf, 8);
}

bool xx_io_write_i64(xx_io_device *dev, int64_t val, bool big_endian) {
    return xx_io_write_u64(dev, (uint64_t)val, big_endian);
}

bool xx_io_write_f32(xx_io_device *dev, float val, bool big_endian) {
    uint32_t u = 0;
    xx_mem_copy(&u, &val, sizeof(float));
    return xx_io_write_u32(dev, u, big_endian);
}

bool xx_io_write_f64(xx_io_device *dev, double val, bool big_endian) {
    uint64_t u = 0;
    xx_mem_copy(&u, &val, sizeof(double));
    return xx_io_write_u64(dev, u, big_endian);
}

bool xx_io_write_ansi_string(xx_io_device *dev, const char *str) {
    if (!dev || !str) {
        return false;
    }
    size_t len = xx_str_len(str);
    return xx_io_write_exact(dev, str, len + 1);
}

bool xx_io_write_unicode_string(xx_io_device *dev, const wchar_t *wstr, bool big_endian) {
    if (!dev || !wstr) {
        return false;
    }
    size_t len = xx_str_wlen(wstr);
    uint8_t *buf = (uint8_t*)xx_mem_alloc((len + 1) * 2);
    if (!buf) {
        return false;
    }
    for (size_t i = 0; i <= len; ++i) {
        xx_data_set_u16(buf, (len + 1) * 2, i * 2, (uint16_t)wstr[i], big_endian);
    }
    bool ok = xx_io_write_exact(dev, buf, (len + 1) * 2);
    xx_mem_free(buf);
    return ok;
}

/* ========================================================================= */
/* --- Writing to xx_io_device at offset                                 --- */
/* ========================================================================= */

bool xx_io_set_u8(xx_io_device *dev, int64_t offset, uint8_t val) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_u8(dev, val);
}

bool xx_io_set_i8(xx_io_device *dev, int64_t offset, int8_t val) {
    return xx_io_set_u8(dev, offset, (uint8_t)val);
}

bool xx_io_set_u16(xx_io_device *dev, int64_t offset, uint16_t val, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_u16(dev, val, big_endian);
}

bool xx_io_set_i16(xx_io_device *dev, int64_t offset, int16_t val, bool big_endian) {
    return xx_io_set_u16(dev, offset, (uint16_t)val, big_endian);
}

bool xx_io_set_u24(xx_io_device *dev, int64_t offset, uint32_t val, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_u24(dev, val, big_endian);
}

bool xx_io_set_i24(xx_io_device *dev, int64_t offset, int32_t val, bool big_endian) {
    return xx_io_set_u24(dev, offset, (uint32_t)val, big_endian);
}

bool xx_io_set_u32(xx_io_device *dev, int64_t offset, uint32_t val, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_u32(dev, val, big_endian);
}

bool xx_io_set_i32(xx_io_device *dev, int64_t offset, int32_t val, bool big_endian) {
    return xx_io_set_u32(dev, offset, (uint32_t)val, big_endian);
}

bool xx_io_set_u64(xx_io_device *dev, int64_t offset, uint64_t val, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_u64(dev, val, big_endian);
}

bool xx_io_set_i64(xx_io_device *dev, int64_t offset, int64_t val, bool big_endian) {
    return xx_io_set_u64(dev, offset, (uint64_t)val, big_endian);
}

bool xx_io_set_f32(xx_io_device *dev, int64_t offset, float val, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_f32(dev, val, big_endian);
}

bool xx_io_set_f64(xx_io_device *dev, int64_t offset, double val, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_f64(dev, val, big_endian);
}

bool xx_io_set_ansi_string(xx_io_device *dev, int64_t offset, const char *str) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_ansi_string(dev, str);
}

bool xx_io_set_unicode_string(xx_io_device *dev, int64_t offset, const wchar_t *wstr, bool big_endian) {
    if (!dev || xx_io_seek64(dev, offset, SEEK_SET) != 0) {
        return false;
    }
    return xx_io_write_unicode_string(dev, wstr, big_endian);
}

/* ========================================================================= */
/* --- Finding Types in xx_io_device                                     --- */
/* ========================================================================= */

/* File scratch remains bounded by the captured global setting.  The KMP
 * prefix table describes the pattern, rather than staging input bytes. */
static int64_t xx_io_find_bytes_capped(xx_io_device *dev, int64_t start_offset,
                                     int64_t max_search_len, const void *pattern,
                                     size_t pattern_size, size_t buffer_size,
                                     xx_pd_struct *pd, const char *label) {
    const uint8_t *needle = (const uint8_t *)pattern;
    size_t capacity = xx_get_file_buffer_size();
    uint8_t *chunk;
    size_t *prefix = NULL;
    size_t matched = 0, retained = 0;
    int64_t consumed = 0, found = -1;
    int level;

    if (!dev || !needle || !pattern_size || pattern_size > (size_t)INT64_MAX || xx_pd_is_stopped(pd)) return -1;
    if (!capacity) capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    if (buffer_size && buffer_size < capacity) capacity = buffer_size;
    if (capacity > (SIZE_MAX >> 1)) capacity = SIZE_MAX >> 1;
    if (start_offset < 0) start_offset = 0;
    if (xx_io_seek64(dev, start_offset, SEEK_SET) != 0) return -1;
    chunk = (uint8_t *)xx_mem_alloc(capacity);
    if (!chunk) return -1;

    if (pattern_size >= capacity) {
        size_t i, j = 0;
        if (pattern_size > SIZE_MAX / sizeof(*prefix)) goto cleanup;
        prefix = (size_t *)xx_mem_alloc(pattern_size * sizeof(*prefix));
        if (!prefix) goto cleanup;
        prefix[0] = 0;
        for (i = 1; i < pattern_size; ++i) {
            if (!(i & 255) && xx_pd_is_stopped(pd)) goto cleanup;
            while (j && needle[i] != needle[j]) j = prefix[j - 1];
            if (needle[i] == needle[j]) ++j;
            prefix[i] = j;
        }
    }

    level = xx_pd_enter_level(pd, max_search_len > 0 ? (uint64_t)max_search_len : 0, label);
    while (!xx_pd_is_stopped(pd)) {
        size_t request = capacity - retained;
        ssize_t n;
        if (max_search_len > 0) {
            int64_t remaining = max_search_len - consumed;
            if (remaining <= 0) break;
            if ((uint64_t)remaining < (uint64_t)request) request = (size_t)remaining;
        }
        if ((uint64_t)(INT64_MAX - start_offset - consumed) < (uint64_t)request)
            request = (size_t)(INT64_MAX - start_offset - consumed);
        if (!request) break;
        n = xx_io_read(dev, chunk + retained, request);
        if (n <= 0 || (size_t)n > request) break;

        if (prefix) {
            size_t i;
            for (i = 0; i < (size_t)n; ++i) {
                if (!(i & 255) && xx_pd_is_stopped(pd)) break;
                while (matched && chunk[i] != needle[matched]) matched = prefix[matched - 1];
                if (chunk[i] == needle[matched]) ++matched;
                if (matched == pattern_size) {
                    found = start_offset + consumed + (int64_t)i + 1 - (int64_t)pattern_size;
                    break;
                }
            }
            consumed += (int64_t)n;
            if (found >= 0 || xx_pd_is_stopped(pd)) break;
        } else {
            size_t available = retained + (size_t)n;
            int64_t index = -1;
            if (available >= pattern_size)
                index = xx_data_find_bytes_buffer_optimize(chunk, available, 0, needle, pattern_size, pd);
            if (index >= 0) {
                found = start_offset + consumed - (int64_t)retained + index;
                break;
            }
            consumed += (int64_t)n;
            if (xx_pd_is_stopped(pd)) break;
            retained = available < pattern_size - 1 ? available : pattern_size - 1;
            xx_mem_move(chunk, chunk + available - retained, retained);
        }
        xx_pd_set_current(pd, level, (uint64_t)consumed);
    }
    if (found >= 0) xx_pd_set_current(pd, level, (uint64_t)(found - start_offset));
    xx_pd_leave_level(pd, level);
cleanup:
    xx_mem_free(prefix);
    xx_mem_free(chunk);
    return found;
}

int64_t xx_io_find_bytes(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size, xx_pd_struct *pd) {
    return xx_io_find_bytes_capped(dev, start_offset, max_search_len, pattern, pattern_size, 0, pd, "Device Find Bytes");
}

int64_t xx_io_find_u8(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint8_t val, xx_pd_struct *pd) {
    return xx_io_find_bytes(dev, start_offset, max_search_len, &val, 1, pd);
}

int64_t xx_io_find_u16(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint16_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[2];
    xx_data_set_u16(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes(dev, start_offset, max_search_len, buf, 2, pd);
}

int64_t xx_io_find_u24(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[3];
    xx_data_set_u24(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes(dev, start_offset, max_search_len, buf, 3, pd);
}

int64_t xx_io_find_u32(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[4];
    xx_data_set_u32(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes(dev, start_offset, max_search_len, buf, 4, pd);
}

int64_t xx_io_find_u64(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint64_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[8];
    xx_data_set_u64(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes(dev, start_offset, max_search_len, buf, 8, pd);
}

int64_t xx_io_find_f32(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, float val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[4];
    xx_data_set_f32(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes(dev, start_offset, max_search_len, buf, 4, pd);
}

int64_t xx_io_find_f64(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, double val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[8];
    xx_data_set_f64(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes(dev, start_offset, max_search_len, buf, 8, pd);
}

int64_t xx_io_find_ansi_string(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const char *str, xx_pd_struct *pd) {
    if (!str) {
        return -1;
    }
    size_t len = xx_str_len(str);
    if (len == 0) {
        return start_offset;
    }
    return xx_io_find_bytes(dev, start_offset, max_search_len, str, len, pd);
}

int64_t xx_io_find_unicode_string(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const wchar_t *wstr, bool big_endian, xx_pd_struct *pd) {
    if (!wstr) {
        return -1;
    }
    size_t len = xx_str_wlen(wstr);
    if (len == 0) {
        return start_offset;
    }
    uint8_t *pat = (uint8_t*)xx_mem_alloc(len * 2);
    if (!pat) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        xx_data_set_u16(pat, len * 2, i * 2, (uint16_t)wstr[i], big_endian);
    }
    int64_t res = xx_io_find_bytes(dev, start_offset, max_search_len, pat, len * 2, pd);
    xx_mem_free(pat);
    return res;
}

/* ========================================================================= */
/* --- Optimized Finding Types in xx_io_device (Large Buffer + Fast Scan)--- */
/* ========================================================================= */

int64_t xx_io_find_bytes_buffer_optimize_ex(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size, size_t buffer_size, xx_pd_struct *pd) {
    return xx_io_find_bytes_capped(dev, start_offset, max_search_len, pattern, pattern_size, buffer_size, pd, "Device Find Bytes Optimized");
}

int64_t xx_io_find_bytes_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size, xx_pd_struct *pd) {
    return xx_io_find_bytes_buffer_optimize_ex(dev, start_offset, max_search_len, pattern, pattern_size, 0, pd);
}

int64_t xx_io_find_u8_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint8_t val, xx_pd_struct *pd) {
    return xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, &val, 1, pd);
}

int64_t xx_io_find_u16_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint16_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[2];
    xx_data_set_u16(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, buf, 2, pd);
}

int64_t xx_io_find_u24_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[3];
    xx_data_set_u24(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, buf, 3, pd);
}

int64_t xx_io_find_u32_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint32_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[4];
    xx_data_set_u32(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, buf, 4, pd);
}

int64_t xx_io_find_u64_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, uint64_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[8];
    xx_data_set_u64(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, buf, 8, pd);
}

int64_t xx_io_find_f32_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, float val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[4];
    xx_data_set_f32(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, buf, 4, pd);
}

int64_t xx_io_find_f64_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, double val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[8];
    xx_data_set_f64(buf, sizeof(buf), 0, val, big_endian);
    return xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, buf, 8, pd);
}

int64_t xx_io_find_ansi_string_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const char *str, xx_pd_struct *pd) {
    if (!str) {
        return -1;
    }
    size_t len = xx_str_len(str);
    if (len == 0) {
        return start_offset;
    }
    return xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, str, len, pd);
}

int64_t xx_io_find_unicode_string_buffer_optimize(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const wchar_t *wstr, bool big_endian, xx_pd_struct *pd) {
    if (!wstr) {
        return -1;
    }
    size_t len = xx_str_wlen(wstr);
    if (len == 0) {
        return start_offset;
    }
    uint8_t *pat = (uint8_t*)xx_mem_alloc(len * 2);
    if (!pat) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        xx_data_set_u16(pat, len * 2, i * 2, (uint16_t)wstr[i], big_endian);
    }
    int64_t res = xx_io_find_bytes_buffer_optimize(dev, start_offset, max_search_len, pat, len * 2, pd);
    xx_mem_free(pat);
    return res;
}
