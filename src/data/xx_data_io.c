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

static bool xx_io_read_exact(xx_io_device *dev, void *buf, size_t n) {
    if (!dev || !buf || n == 0) {
        return false;
    }
    uint8_t *p = (uint8_t*)buf;
    size_t total = 0;
    while (total < n) {
        ssize_t r = xx_io_read(dev, p + total, n - total);
        if (r <= 0) {
            return false;
        }
        total += (size_t)r;
    }
    return true;
}

static bool xx_io_write_exact(xx_io_device *dev, const void *buf, size_t n) {
    if (!dev || !buf || n == 0) {
        return false;
    }
    const uint8_t *p = (const uint8_t*)buf;
    size_t total = 0;
    while (total < n) {
        ssize_t w = xx_io_write(dev, p + total, n - total);
        if (w <= 0) {
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
        if (xx_io_read(dev, buf, 2) != 2) {
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

int64_t xx_io_find_bytes(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size, xx_pd_struct *pd) {
    if (!dev || !pattern || pattern_size == 0) {
        return -1;
    }
    if (xx_pd_is_stopped(pd)) {
        return -1;
    }
    if (start_offset < 0) {
        start_offset = 0;
    }
    if (xx_io_seek64(dev, start_offset, SEEK_SET) != 0) {
        return -1;
    }

    size_t chunk_capacity = xx_get_file_buffer_size();
    if (chunk_capacity == 0) {
        chunk_capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    }
    if (pattern_size > chunk_capacity / 2) {
        chunk_capacity = pattern_size * 2;
    }
    uint8_t *chunk = (uint8_t*)xx_mem_alloc(chunk_capacity);
    if (!chunk) {
        return -1;
    }

    uint64_t total_expected = (max_search_len > 0) ? (uint64_t)max_search_len : 0;
    int level = xx_pd_enter_level(pd, total_expected, "Device Find Bytes");

    size_t overlap = pattern_size - 1;
    int64_t current_file_offset = start_offset;
    int64_t total_searched = 0;
    int64_t found_offset = -1;
    size_t bytes_in_buf = 0;

    while (1) {
        if (xx_pd_is_stopped(pd)) {
            break;
        }

        size_t to_read = chunk_capacity - bytes_in_buf;
        if (max_search_len > 0) {
            int64_t remaining = max_search_len - total_searched;
            if (remaining <= 0) {
                break;
            }
            if ((size_t)remaining < to_read) {
                to_read = (size_t)remaining;
            }
        }

        if (to_read == 0) {
            break;
        }

        ssize_t bytes_read = xx_io_read(dev, chunk + bytes_in_buf, to_read);
        if (bytes_read <= 0) {
            break;
        }

        bytes_in_buf += (size_t)bytes_read;

        if (bytes_in_buf >= pattern_size) {
            int64_t idx = xx_data_find_bytes_buffer_optimize(chunk, bytes_in_buf, 0, pattern, pattern_size, pd);
            if (idx >= 0) {
                found_offset = current_file_offset + idx;
                break;
            }
        }

        if (xx_pd_is_stopped(pd)) {
            break;
        }

        if (bytes_in_buf > overlap) {
            size_t advanced = bytes_in_buf - overlap;
            current_file_offset += advanced;
            total_searched += advanced;
            xx_pd_set_current(pd, level, (uint64_t)total_searched);
            xx_mem_copy(chunk, chunk + advanced, overlap);
            bytes_in_buf = overlap;
        } else {
            break;
        }
    }

    xx_mem_free(chunk);
    if (found_offset >= 0) {
        xx_pd_set_current(pd, level, (uint64_t)(found_offset - start_offset));
    }
    xx_pd_leave_level(pd, level);
    return found_offset;
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

#define XX_FIND_BUFFER_OPTIMIZE_DEFAULT_SIZE (64 * 1024)

int64_t xx_io_find_bytes_buffer_optimize_ex(xx_io_device *dev, int64_t start_offset, int64_t max_search_len, const void *pattern, size_t pattern_size, size_t buffer_size, xx_pd_struct *pd) {
    if (!dev || !pattern || pattern_size == 0) {
        return -1;
    }
    if (xx_pd_is_stopped(pd)) {
        return -1;
    }
    if (start_offset < 0) {
        start_offset = 0;
    }
    if (xx_io_seek64(dev, start_offset, SEEK_SET) != 0) {
        return -1;
    }

    size_t chunk_capacity = (buffer_size > 0) ? buffer_size : xx_get_file_buffer_size();
    if (chunk_capacity == 0) {
        chunk_capacity = XX_DEFAULT_FILE_BUFFER_SIZE;
    }
    if (pattern_size > chunk_capacity / 2) {
        chunk_capacity = pattern_size * 2;
    }
    uint8_t *chunk = (uint8_t*)xx_mem_alloc(chunk_capacity);
    if (!chunk) {
        return -1;
    }

    uint64_t total_expected = (max_search_len > 0) ? (uint64_t)max_search_len : 0;
    int level = xx_pd_enter_level(pd, total_expected, "Device Find Bytes Optimized");

    size_t overlap = pattern_size - 1;
    int64_t current_file_offset = start_offset;
    int64_t total_searched = 0;
    int64_t found_offset = -1;
    size_t bytes_in_buf = 0;

    while (1) {
        if (xx_pd_is_stopped(pd)) {
            break;
        }

        size_t to_read = chunk_capacity - bytes_in_buf;
        if (max_search_len > 0) {
            int64_t remaining = max_search_len - total_searched;
            if (remaining <= 0) {
                break;
            }
            if ((size_t)remaining < to_read) {
                to_read = (size_t)remaining;
            }
        }

        if (to_read == 0) {
            break;
        }

        ssize_t bytes_read = xx_io_read(dev, chunk + bytes_in_buf, to_read);
        if (bytes_read <= 0) {
            break;
        }

        bytes_in_buf += (size_t)bytes_read;

        if (bytes_in_buf >= pattern_size) {
            int64_t idx = xx_data_find_bytes_buffer_optimize(chunk, bytes_in_buf, 0, pattern, pattern_size, pd);
            if (idx >= 0) {
                found_offset = current_file_offset + idx;
                break;
            }
        }

        if (xx_pd_is_stopped(pd)) {
            break;
        }

        if (bytes_in_buf > overlap) {
            size_t advanced = bytes_in_buf - overlap;
            current_file_offset += advanced;
            total_searched += advanced;
            xx_pd_set_current(pd, level, (uint64_t)total_searched);
            xx_mem_copy(chunk, chunk + advanced, overlap);
            bytes_in_buf = overlap;
        } else {
            break;
        }
    }

    xx_mem_free(chunk);
    if (found_offset >= 0) {
        xx_pd_set_current(pd, level, (uint64_t)(found_offset - start_offset));
    }
    xx_pd_leave_level(pd, level);
    return found_offset;
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
