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
 * @file xx_global.c
 * @brief Global buffer and file buffer size configuration implementation.
 */

#include "xxfclib/global/xx_global.h"

static size_t g_buffer_size = XX_DEFAULT_BUFFER_SIZE;
static size_t g_file_buffer_size = XX_DEFAULT_FILE_BUFFER_SIZE;

void xx_set_buffer_size(size_t size) {
    if (size == 0) {
        g_buffer_size = XX_DEFAULT_BUFFER_SIZE;
    } else {
        g_buffer_size = size;
    }
}

size_t xx_get_buffer_size(void) {
    return g_buffer_size;
}

void xx_set_file_buffer_size(size_t size) {
    if (size == 0) {
        g_file_buffer_size = XX_DEFAULT_FILE_BUFFER_SIZE;
    } else {
        g_file_buffer_size = size;
    }
}

size_t xx_get_file_buffer_size(void) {
    return g_file_buffer_size;
}

void xx_global_set_buffer_size(size_t size) {
    xx_set_buffer_size(size);
}

size_t xx_global_get_buffer_size(void) {
    return xx_get_buffer_size();
}

void xx_global_set_file_buffer_size(size_t size) {
    xx_set_file_buffer_size(size);
}

size_t xx_global_get_file_buffer_size(void) {
    return xx_get_file_buffer_size();
}

#include "platforms/xx_global_platform.h"

static bool g_sse2_detected = false;
static bool g_avx2_detected = false;
static bool g_sse2_enabled  = false;
static bool g_avx2_enabled  = false;
static bool g_features_initialized = false;

static void xx_global_init_features_once(void) {
    if (!g_features_initialized) {
        g_sse2_detected = xx_global_platform_has_sse2();
        g_avx2_detected = xx_global_platform_has_avx2();
        /* Default is on if feature exists in system */
        g_sse2_enabled = g_sse2_detected;
        g_avx2_enabled = g_avx2_detected;
        g_features_initialized = true;
    }
}

bool xx_has_sse2(void) {
    xx_global_init_features_once();
    return g_sse2_detected;
}

bool xx_has_avx2(void) {
    xx_global_init_features_once();
    return g_avx2_detected;
}

void xx_set_sse2_enabled(bool enable) {
    xx_global_init_features_once();
    g_sse2_enabled = enable && g_sse2_detected;
}

void xx_enable_sse2(bool enable) {
    xx_set_sse2_enabled(enable);
}

bool xx_is_sse2_enabled(void) {
    xx_global_init_features_once();
    return g_sse2_enabled && g_sse2_detected;
}

void xx_set_avx2_enabled(bool enable) {
    xx_global_init_features_once();
    g_avx2_enabled = enable && g_avx2_detected;
}

void xx_enable_avx2(bool enable) {
    xx_set_avx2_enabled(enable);
}

bool xx_is_avx2_enabled(void) {
    xx_global_init_features_once();
    return g_avx2_enabled && g_avx2_detected;
}

/* Aliases with xx_global prefix */
bool xx_global_has_sse2(void) {
    return xx_has_sse2();
}

bool xx_global_has_avx2(void) {
    return xx_has_avx2();
}

void xx_global_set_sse2_enabled(bool enable) {
    xx_set_sse2_enabled(enable);
}

void xx_global_enable_sse2(bool enable) {
    xx_enable_sse2(enable);
}

bool xx_global_is_sse2_enabled(void) {
    return xx_is_sse2_enabled();
}

void xx_global_set_avx2_enabled(bool enable) {
    xx_set_avx2_enabled(enable);
}

void xx_global_enable_avx2(bool enable) {
    xx_enable_avx2(enable);
}

bool xx_global_is_avx2_enabled(void) {
    return xx_is_avx2_enabled();
}
