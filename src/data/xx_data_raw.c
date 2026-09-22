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
 * @file xx_data_raw.c
 * @brief Raw memory buffer data reading, writing, and search implementation.
 */

#include "xxfclib/data/xx_data.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/global/xx_global.h"

#include "platforms/xx_data_platform.h"

/* ========================================================================= */
/* --- Reading from Raw Memory Buffer                                    --- */
/* ========================================================================= */

uint8_t xx_data_get_u8(const void *data, size_t data_size, size_t offset) {
    if (!data || offset >= data_size) {
        return 0;
    }
    return ((const uint8_t*)data)[offset];
}

int8_t xx_data_get_i8(const void *data, size_t data_size, size_t offset) {
    return (int8_t)xx_data_get_u8(data, data_size, offset);
}

uint16_t xx_data_get_u16(const void *data, size_t data_size, size_t offset, bool big_endian) {
    if (!data || offset + 2 > data_size || offset + 2 < offset) {
        return 0;
    }
    const uint8_t *b = (const uint8_t*)data + offset;
    if (big_endian) {
        return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
    }
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

int16_t xx_data_get_i16(const void *data, size_t data_size, size_t offset, bool big_endian) {
    return (int16_t)xx_data_get_u16(data, data_size, offset, big_endian);
}

uint32_t xx_data_get_u24(const void *data, size_t data_size, size_t offset, bool big_endian) {
    if (!data || offset + 3 > data_size || offset + 3 < offset) {
        return 0;
    }
    const uint8_t *b = (const uint8_t*)data + offset;
    if (big_endian) {
        return ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
    }
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
}

int32_t xx_data_get_i24(const void *data, size_t data_size, size_t offset, bool big_endian) {
    uint32_t val = xx_data_get_u24(data, data_size, offset, big_endian);
    if (val & 0x00800000) {
        val |= 0xFF000000;
    }
    return (int32_t)val;
}

uint32_t xx_data_get_u32(const void *data, size_t data_size, size_t offset, bool big_endian) {
    if (!data || offset + 4 > data_size || offset + 4 < offset) {
        return 0;
    }
    const uint8_t *b = (const uint8_t*)data + offset;
    if (big_endian) {
        return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
               ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
    }
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

int32_t xx_data_get_i32(const void *data, size_t data_size, size_t offset, bool big_endian) {
    return (int32_t)xx_data_get_u32(data, data_size, offset, big_endian);
}

uint64_t xx_data_get_u64(const void *data, size_t data_size, size_t offset, bool big_endian) {
    if (!data || offset + 8 > data_size || offset + 8 < offset) {
        return 0;
    }
    const uint8_t *b = (const uint8_t*)data + offset;
    if (big_endian) {
        return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
               ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
               ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
               ((uint64_t)b[6] << 8)  | (uint64_t)b[7];
    }
    return (uint64_t)b[0] | ((uint64_t)b[1] << 8)  |
           ((uint64_t)b[2] << 16) | ((uint64_t)b[3] << 24) |
           ((uint64_t)b[4] << 32) | ((uint64_t)b[5] << 40) |
           ((uint64_t)b[6] << 48) | ((uint64_t)b[7] << 56);
}

int64_t xx_data_get_i64(const void *data, size_t data_size, size_t offset, bool big_endian) {
    return (int64_t)xx_data_get_u64(data, data_size, offset, big_endian);
}

float xx_data_get_f32(const void *data, size_t data_size, size_t offset, bool big_endian) {
    uint32_t u = xx_data_get_u32(data, data_size, offset, big_endian);
    float f = 0.0f;
    xx_mem_copy(&f, &u, sizeof(float));
    return f;
}

double xx_data_get_f64(const void *data, size_t data_size, size_t offset, bool big_endian) {
    uint64_t u = xx_data_get_u64(data, data_size, offset, big_endian);
    double d = 0.0;
    xx_mem_copy(&d, &u, sizeof(double));
    return d;
}

float xx_data_get_f16(const void *data, size_t data_size, size_t offset, bool big_endian) {
    /* Expand an IEEE-754 half (1 sign, 5 exponent, 10 fraction) into a
     * single. There is no host type to read it into, so the bit pattern is
     * assembled by hand and reinterpreted, exactly as f32/f64 above do. */
    uint16_t half = xx_data_get_u16(data, data_size, offset, big_endian);
    uint32_t sign = (uint32_t)(half >> 15);
    uint32_t exponent = (uint32_t)((half >> 10) & 0x1FU);
    uint32_t fraction = (uint32_t)(half & 0x3FFU);
    uint32_t value;
    float f = 0.0f;

    if (exponent == 0U) {
        if (fraction == 0U) {
            /* Signed zero. */
            value = sign << 31;
        } else {
            /* Subnormal: renormalise by shifting the fraction left until the
             * implicit bit appears, paying one exponent step per shift. */
            exponent = 127U - 14U;
            while ((fraction & (1U << 10)) == 0U) {
                --exponent;
                fraction <<= 1;
            }
            fraction &= 0x3FFU;
            value = (sign << 31) | (exponent << 23) | (fraction << 13);
        }
    } else if (exponent == 0x1FU) {
        /* Infinity and NaN keep their payload. */
        value = (sign << 31) | ((uint32_t)0xFFU << 23) | (fraction << 13);
    } else {
        /* Normal: rebias from half's 15 to single's 127. */
        value = (sign << 31) | ((exponent + (127U - 15U)) << 23) | (fraction << 13);
    }

    xx_mem_copy(&f, &value, sizeof(float));
    return f;
}

char* xx_data_get_ansi_string(const void *data, size_t data_size, size_t offset, size_t max_len) {
    if (!data || offset >= data_size) {
        return NULL;
    }
    size_t available = data_size - offset;
    size_t scan_limit = (max_len > 0 && max_len < available) ? max_len : available;
    const char *src = (const char*)data + offset;
    size_t len = 0;
    while (len < scan_limit && src[len] != '\0') {
        len++;
    }

    char *res = (char*)xx_mem_alloc(len + 1);
    if (!res) {
        return NULL;
    }
    if (len > 0) {
        xx_mem_copy(res, src, len);
    }
    res[len] = '\0';
    return res;
}

wchar_t* xx_data_get_unicode_string(const void *data, size_t data_size, size_t offset, size_t max_len, bool big_endian) {
    if (!data || offset + 2 > data_size) {
        return NULL;
    }
    size_t available_bytes = data_size - offset;
    size_t available_units = available_bytes / 2;
    size_t scan_limit = (max_len > 0 && max_len < available_units) ? max_len : available_units;

    size_t units = 0;
    while (units < scan_limit) {
        uint16_t ch = xx_data_get_u16(data, data_size, offset + units * 2, big_endian);
        if (ch == 0) {
            break;
        }
        units++;
    }

    wchar_t *res = (wchar_t*)xx_mem_alloc((units + 1) * sizeof(wchar_t));
    if (!res) {
        return NULL;
    }
    for (size_t i = 0; i < units; ++i) {
        res[i] = (wchar_t)xx_data_get_u16(data, data_size, offset + i * 2, big_endian);
    }
    res[units] = L'\0';
    return res;
}

/* ========================================================================= */
/* --- Writing to Raw Memory Buffer                                      --- */
/* ========================================================================= */

bool xx_data_set_u8(void *data, size_t data_size, size_t offset, uint8_t val) {
    if (!data || offset >= data_size) {
        return false;
    }
    ((uint8_t*)data)[offset] = val;
    return true;
}

bool xx_data_set_i8(void *data, size_t data_size, size_t offset, int8_t val) {
    return xx_data_set_u8(data, data_size, offset, (uint8_t)val);
}

bool xx_data_set_u16(void *data, size_t data_size, size_t offset, uint16_t val, bool big_endian) {
    if (!data || offset + 2 > data_size || offset + 2 < offset) {
        return false;
    }
    uint8_t *b = (uint8_t*)data + offset;
    if (big_endian) {
        b[0] = (uint8_t)((val >> 8) & 0xFF);
        b[1] = (uint8_t)(val & 0xFF);
    } else {
        b[0] = (uint8_t)(val & 0xFF);
        b[1] = (uint8_t)((val >> 8) & 0xFF);
    }
    return true;
}

bool xx_data_set_i16(void *data, size_t data_size, size_t offset, int16_t val, bool big_endian) {
    return xx_data_set_u16(data, data_size, offset, (uint16_t)val, big_endian);
}

bool xx_data_set_u24(void *data, size_t data_size, size_t offset, uint32_t val, bool big_endian) {
    if (!data || offset + 3 > data_size || offset + 3 < offset) {
        return false;
    }
    uint8_t *b = (uint8_t*)data + offset;
    if (big_endian) {
        b[0] = (uint8_t)((val >> 16) & 0xFF);
        b[1] = (uint8_t)((val >> 8) & 0xFF);
        b[2] = (uint8_t)(val & 0xFF);
    } else {
        b[0] = (uint8_t)(val & 0xFF);
        b[1] = (uint8_t)((val >> 8) & 0xFF);
        b[2] = (uint8_t)((val >> 16) & 0xFF);
    }
    return true;
}

bool xx_data_set_i24(void *data, size_t data_size, size_t offset, int32_t val, bool big_endian) {
    return xx_data_set_u24(data, data_size, offset, (uint32_t)val, big_endian);
}

bool xx_data_set_u32(void *data, size_t data_size, size_t offset, uint32_t val, bool big_endian) {
    if (!data || offset + 4 > data_size || offset + 4 < offset) {
        return false;
    }
    uint8_t *b = (uint8_t*)data + offset;
    if (big_endian) {
        b[0] = (uint8_t)((val >> 24) & 0xFF);
        b[1] = (uint8_t)((val >> 16) & 0xFF);
        b[2] = (uint8_t)((val >> 8) & 0xFF);
        b[3] = (uint8_t)(val & 0xFF);
    } else {
        b[0] = (uint8_t)(val & 0xFF);
        b[1] = (uint8_t)((val >> 8) & 0xFF);
        b[2] = (uint8_t)((val >> 16) & 0xFF);
        b[3] = (uint8_t)((val >> 24) & 0xFF);
    }
    return true;
}

bool xx_data_set_i32(void *data, size_t data_size, size_t offset, int32_t val, bool big_endian) {
    return xx_data_set_u32(data, data_size, offset, (uint32_t)val, big_endian);
}

bool xx_data_set_u64(void *data, size_t data_size, size_t offset, uint64_t val, bool big_endian) {
    if (!data || offset + 8 > data_size || offset + 8 < offset) {
        return false;
    }
    uint8_t *b = (uint8_t*)data + offset;
    if (big_endian) {
        b[0] = (uint8_t)((val >> 56) & 0xFF);
        b[1] = (uint8_t)((val >> 48) & 0xFF);
        b[2] = (uint8_t)((val >> 40) & 0xFF);
        b[3] = (uint8_t)((val >> 32) & 0xFF);
        b[4] = (uint8_t)((val >> 24) & 0xFF);
        b[5] = (uint8_t)((val >> 16) & 0xFF);
        b[6] = (uint8_t)((val >> 8) & 0xFF);
        b[7] = (uint8_t)(val & 0xFF);
    } else {
        b[0] = (uint8_t)(val & 0xFF);
        b[1] = (uint8_t)((val >> 8) & 0xFF);
        b[2] = (uint8_t)((val >> 16) & 0xFF);
        b[3] = (uint8_t)((val >> 24) & 0xFF);
        b[4] = (uint8_t)((val >> 32) & 0xFF);
        b[5] = (uint8_t)((val >> 40) & 0xFF);
        b[6] = (uint8_t)((val >> 48) & 0xFF);
        b[7] = (uint8_t)((val >> 56) & 0xFF);
    }
    return true;
}

bool xx_data_set_i64(void *data, size_t data_size, size_t offset, int64_t val, bool big_endian) {
    return xx_data_set_u64(data, data_size, offset, (uint64_t)val, big_endian);
}

bool xx_data_set_f32(void *data, size_t data_size, size_t offset, float val, bool big_endian) {
    uint32_t u = 0;
    xx_mem_copy(&u, &val, sizeof(float));
    return xx_data_set_u32(data, data_size, offset, u, big_endian);
}

bool xx_data_set_f64(void *data, size_t data_size, size_t offset, double val, bool big_endian) {
    uint64_t u = 0;
    xx_mem_copy(&u, &val, sizeof(double));
    return xx_data_set_u64(data, data_size, offset, u, big_endian);
}

bool xx_data_set_bytes(void *data, size_t data_size, size_t offset, const void *src, size_t src_size) {
    if (!data || !src || offset + src_size > data_size || offset + src_size < offset) {
        return false;
    }
    if (src_size > 0) {
        xx_mem_copy((uint8_t*)data + offset, src, src_size);
    }
    return true;
}

bool xx_data_set_ansi_string(void *data, size_t data_size, size_t offset, const char *str) {
    if (!str) {
        return false;
    }
    size_t len = xx_str_len(str);
    return xx_data_set_bytes(data, data_size, offset, str, len + 1);
}

bool xx_data_set_unicode_string(void *data, size_t data_size, size_t offset, const wchar_t *wstr, bool big_endian) {
    if (!wstr) {
        return false;
    }
    size_t len = xx_str_wlen(wstr);
    size_t needed_bytes = (len + 1) * 2;
    if (!data || offset + needed_bytes > data_size || offset + needed_bytes < offset) {
        return false;
    }
    for (size_t i = 0; i <= len; ++i) {
        xx_data_set_u16(data, data_size, offset + i * 2, (uint16_t)wstr[i], big_endian);
    }
    return true;
}

/* ========================================================================= */
/* --- Finding Types in Raw Memory Buffer                                --- */
/* ========================================================================= */

int64_t xx_data_find_bytes(const void *data, size_t data_size, size_t start_offset, const void *pattern, size_t pattern_size, xx_pd_struct *pd) {
    if (!data || !pattern || pattern_size == 0 || start_offset + pattern_size > data_size || start_offset + pattern_size < start_offset) {
        return -1;
    }
    if (xx_pd_is_stopped(pd)) {
        return -1;
    }

    int level = xx_pd_enter_level(pd, (uint64_t)data_size, "Find Bytes");

    const uint8_t *pdata = (const uint8_t*)data;
    const uint8_t *pat = (const uint8_t*)pattern;
    uint8_t first = pat[0];
    size_t limit = data_size - pattern_size;
    int64_t found_offset = -1;

    for (size_t i = start_offset; i <= limit; ++i) {
        if ((i & 0x3FFF) == 0) {
            if (xx_pd_is_stopped(pd)) {
                xx_pd_leave_level(pd, level);
                return -1;
            }
            xx_pd_set_current(pd, level, (uint64_t)i);
        }

        if (pdata[i] == first) {
            if (pattern_size == 1 || xx_mem_compare(pdata + i, pat, pattern_size) == 0) {
                found_offset = (int64_t)i;
                break;
            }
        }
    }

    if (found_offset >= 0) {
        xx_pd_set_current(pd, level, (uint64_t)found_offset);
    } else {
        xx_pd_set_current(pd, level, (uint64_t)data_size);
    }
    xx_pd_leave_level(pd, level);
    return found_offset;
}

int64_t xx_data_find_u8(const void *data, size_t data_size, size_t start_offset, uint8_t val, xx_pd_struct *pd) {
    return xx_data_find_bytes(data, data_size, start_offset, &val, 1, pd);
}

int64_t xx_data_find_u16(const void *data, size_t data_size, size_t start_offset, uint16_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[2];
    xx_data_set_u16(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes(data, data_size, start_offset, buf, 2, pd);
}

int64_t xx_data_find_u24(const void *data, size_t data_size, size_t start_offset, uint32_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[3];
    xx_data_set_u24(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes(data, data_size, start_offset, buf, 3, pd);
}

int64_t xx_data_find_u32(const void *data, size_t data_size, size_t start_offset, uint32_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[4];
    xx_data_set_u32(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes(data, data_size, start_offset, buf, 4, pd);
}

int64_t xx_data_find_u64(const void *data, size_t data_size, size_t start_offset, uint64_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[8];
    xx_data_set_u64(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes(data, data_size, start_offset, buf, 8, pd);
}

int64_t xx_data_find_f32(const void *data, size_t data_size, size_t start_offset, float val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[4];
    xx_data_set_f32(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes(data, data_size, start_offset, buf, 4, pd);
}

int64_t xx_data_find_f64(const void *data, size_t data_size, size_t start_offset, double val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[8];
    xx_data_set_f64(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes(data, data_size, start_offset, buf, 8, pd);
}

int64_t xx_data_find_ansi_string(const void *data, size_t data_size, size_t start_offset, const char *str, xx_pd_struct *pd) {
    if (!str) {
        return -1;
    }
    size_t len = xx_str_len(str);
    if (len == 0) {
        return (int64_t)start_offset;
    }
    return xx_data_find_bytes(data, data_size, start_offset, str, len, pd);
}

int64_t xx_data_find_unicode_string(const void *data, size_t data_size, size_t start_offset, const wchar_t *wstr, bool big_endian, xx_pd_struct *pd) {
    if (!wstr) {
        return -1;
    }
    size_t len = xx_str_wlen(wstr);
    if (len == 0) {
        return (int64_t)start_offset;
    }
    uint8_t *pat = (uint8_t*)xx_mem_alloc(len * 2);
    if (!pat) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        xx_data_set_u16(pat, len * 2, i * 2, (uint16_t)wstr[i], big_endian);
    }

    int64_t res = xx_data_find_bytes(data, data_size, start_offset, pat, len * 2, pd);
    xx_mem_free(pat);
    return res;
}

/* ========================================================================= */
/* --- Optimized Finding Types in Raw Memory Buffer (Sunday / Fast Word) --- */
/* ========================================================================= */

int64_t xx_data_find_bytes_buffer_optimize(const void *data, size_t data_size, size_t start_offset, const void *pattern, size_t pattern_size, xx_pd_struct *pd) {
    if (!data || !pattern || pattern_size == 0 || start_offset + pattern_size > data_size || start_offset + pattern_size < start_offset) {
        return -1;
    }
    if (xx_pd_is_stopped(pd)) {
        return -1;
    }

    int level = xx_pd_enter_level(pd, (uint64_t)data_size, "Find Bytes Optimized");

    const uint8_t *pdata = (const uint8_t*)data;
    const uint8_t *pat = (const uint8_t*)pattern;
    size_t limit = data_size - pattern_size;
    int64_t found_offset = -1;

    /* 1-byte pattern fast scan */
    if (pattern_size == 1) {
        uint8_t target = pat[0];
        size_t i = start_offset;

        if (xx_is_avx2_enabled() && (limit >= start_offset + 32)) {
            found_offset = xx_data_find_bytes_avx2(pdata, data_size, start_offset, pat, 1, pd);
            xx_pd_leave_level(pd, level);
            return found_offset;
        }
        if (xx_is_sse2_enabled() && (limit >= start_offset + 16)) {
            found_offset = xx_data_find_bytes_sse2(pdata, data_size, start_offset, pat, 1, pd);
            xx_pd_leave_level(pd, level);
            return found_offset;
        }

        /* Align to 8-byte boundary */
        while (i <= limit && (((uintptr_t)(pdata + i)) & 7) != 0) {
            if (pdata[i] == target) {
                found_offset = (int64_t)i;
                break;
            }
            i++;
        }

        if (found_offset < 0) {
            /* 64-bit word scan */
            uint64_t repeated = (uint64_t)target * 0x0101010101010101ULL;
            while (i + 8 <= limit + 1) {
                if ((i & 0x7FFF) == 0) {
                    if (xx_pd_is_stopped(pd)) {
                        xx_pd_leave_level(pd, level);
                        return -1;
                    }
                    xx_pd_set_current(pd, level, (uint64_t)i);
                }

                uint64_t val;
                xx_mem_copy(&val, pdata + i, sizeof(uint64_t));
                uint64_t xor_val = val ^ repeated;
                if (((xor_val - 0x0101010101010101ULL) & ~xor_val & 0x8080808080808080ULL) != 0) {
                    for (size_t b = 0; b < 8; ++b) {
                        if (pdata[i + b] == target) {
                            found_offset = (int64_t)(i + b);
                            break;
                        }
                    }
                    if (found_offset >= 0) {
                        break;
                    }
                }
                i += 8;
            }
        }

        /* Trailing bytes */
        if (found_offset < 0) {
            while (i <= limit) {
                if (pdata[i] == target) {
                    found_offset = (int64_t)i;
                    break;
                }
                i++;
            }
        }

        if (found_offset >= 0) {
            xx_pd_set_current(pd, level, (uint64_t)found_offset);
        } else {
            xx_pd_set_current(pd, level, (uint64_t)data_size);
        }
        xx_pd_leave_level(pd, level);
        return found_offset;
    }

    /* AVX2 SIMD First-and-Last Byte Filter (if globally enabled and supported) */
    if (xx_is_avx2_enabled() && (limit >= start_offset + 32)) {
        found_offset = xx_data_find_bytes_avx2(pdata, data_size, start_offset, pat, pattern_size, pd);
        xx_pd_leave_level(pd, level);
        return found_offset;
    }

    /* SSE2 SIMD First-and-Last Byte Filter (if globally enabled and supported) */
    if (xx_is_sse2_enabled() && (limit >= start_offset + 16)) {
        found_offset = xx_data_find_bytes_sse2(pdata, data_size, start_offset, pat, pattern_size, pd);
        xx_pd_leave_level(pd, level);
        return found_offset;
    }

    /* Scalar Sunday Quick-Search algorithm for pattern_size >= 2 (fallback when SIMD is disabled) */
    size_t shift[256];
    size_t default_shift = pattern_size + 1;
    for (size_t c = 0; c < 256; ++c) {
        shift[c] = default_shift;
    }
    for (size_t c = 0; c < pattern_size; ++c) {
        shift[pat[c]] = pattern_size - c;
    }

    uint8_t first = pat[0];
    uint8_t last = pat[pattern_size - 1];
    size_t i = start_offset;

    while (i <= limit) {
        if ((i & 0x7FFF) == 0) {
            if (xx_pd_is_stopped(pd)) {
                xx_pd_leave_level(pd, level);
                return -1;
            }
            xx_pd_set_current(pd, level, (uint64_t)i);
        }

        if (pdata[i] == first && pdata[i + pattern_size - 1] == last) {
            if (pattern_size <= 2 || xx_mem_compare(pdata + i + 1, pat + 1, pattern_size - 2) == 0) {
                found_offset = (int64_t)i;
                break;
            }
        }

        if (i + pattern_size < data_size) {
            i += shift[pdata[i + pattern_size]];
        } else {
            break;
        }
    }

    if (found_offset >= 0) {
        xx_pd_set_current(pd, level, (uint64_t)found_offset);
    } else {
        xx_pd_set_current(pd, level, (uint64_t)data_size);
    }
    xx_pd_leave_level(pd, level);
    return found_offset;
}

int64_t xx_data_find_u8_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint8_t val, xx_pd_struct *pd) {
    return xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, &val, 1, pd);
}

int64_t xx_data_find_u16_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint16_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[2];
    xx_data_set_u16(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, buf, 2, pd);
}

int64_t xx_data_find_u24_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint32_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[3];
    xx_data_set_u24(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, buf, 3, pd);
}

int64_t xx_data_find_u32_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint32_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[4];
    xx_data_set_u32(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, buf, 4, pd);
}

int64_t xx_data_find_u64_buffer_optimize(const void *data, size_t data_size, size_t start_offset, uint64_t val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[8];
    xx_data_set_u64(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, buf, 8, pd);
}

int64_t xx_data_find_f32_buffer_optimize(const void *data, size_t data_size, size_t start_offset, float val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[4];
    xx_data_set_f32(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, buf, 4, pd);
}

int64_t xx_data_find_f64_buffer_optimize(const void *data, size_t data_size, size_t start_offset, double val, bool big_endian, xx_pd_struct *pd) {
    uint8_t buf[8];
    xx_data_set_f64(buf, sizeof(buf), 0, val, big_endian);
    return xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, buf, 8, pd);
}

int64_t xx_data_find_ansi_string_buffer_optimize(const void *data, size_t data_size, size_t start_offset, const char *str, xx_pd_struct *pd) {
    if (!str) {
        return -1;
    }
    size_t len = xx_str_len(str);
    if (len == 0) {
        return (int64_t)start_offset;
    }
    return xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, str, len, pd);
}

int64_t xx_data_find_unicode_string_buffer_optimize(const void *data, size_t data_size, size_t start_offset, const wchar_t *wstr, bool big_endian, xx_pd_struct *pd) {
    if (!wstr) {
        return -1;
    }
    size_t len = xx_str_wlen(wstr);
    if (len == 0) {
        return (int64_t)start_offset;
    }
    uint8_t *pat = (uint8_t*)xx_mem_alloc(len * 2);
    if (!pat) {
        return -1;
    }
    for (size_t i = 0; i < len; ++i) {
        xx_data_set_u16(pat, len * 2, i * 2, (uint16_t)wstr[i], big_endian);
    }

    int64_t res = xx_data_find_bytes_buffer_optimize(data, data_size, start_offset, pat, len * 2, pd);
    xx_mem_free(pat);
    return res;
}
