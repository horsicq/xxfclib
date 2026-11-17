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
 * @brief Global buffer, CPU feature and terminal configuration.
 */

#include "xxfclib/global/xx_global.h"

static size_t g_buffer_size = XX_DEFAULT_BUFFER_SIZE;
static size_t g_file_buffer_size = XX_DEFAULT_FILE_BUFFER_SIZE;

void xx_set_buffer_size(size_t size)
{
    if (size == 0) {
        g_buffer_size = XX_DEFAULT_BUFFER_SIZE;
    } else {
        g_buffer_size = size;
    }
}

size_t xx_get_buffer_size(void)
{
    return g_buffer_size;
}

void xx_set_file_buffer_size(size_t size)
{
    if (size == 0) {
        g_file_buffer_size = XX_DEFAULT_FILE_BUFFER_SIZE;
    } else {
        g_file_buffer_size = size;
    }
}

size_t xx_get_file_buffer_size(void)
{
    return g_file_buffer_size;
}

void xx_global_set_buffer_size(size_t size)
{
    xx_set_buffer_size(size);
}

size_t xx_global_get_buffer_size(void)
{
    return xx_get_buffer_size();
}

void xx_global_set_file_buffer_size(size_t size)
{
    xx_set_file_buffer_size(size);
}

size_t xx_global_get_file_buffer_size(void)
{
    return xx_get_file_buffer_size();
}

static uint64_t g_file_memory_limit = XX_DEFAULT_FILE_MEMORY_LIMIT;

void xx_set_file_memory_limit(uint64_t limit)
{
    g_file_memory_limit = limit;
}

uint64_t xx_get_file_memory_limit(void)
{
    return g_file_memory_limit;
}

#include "platforms/xx_global_platform.h"

static bool g_sse2_detected = false;
static bool g_sse42_detected = false;
static bool g_avx2_detected = false;
static bool g_aes_detected = false;
static bool g_sha_detected = false;
static bool g_vpclmulqdq_detected = false;
static bool g_neon_detected = false;
static bool g_arm_crc32_detected = false;
static bool g_arm_crypto_detected = false;
static bool g_arm_dotprod_detected = false;
static bool g_arm_pmull_detected = false;
static bool g_sse2_enabled = false;
static bool g_sse42_enabled = false;
static bool g_avx2_enabled = false;
static bool g_aes_enabled = false;
static bool g_sha_enabled = false;
static bool g_vpclmulqdq_enabled = false;
static bool g_neon_enabled = false;
static bool g_arm_crc32_enabled = false;
static bool g_arm_crypto_enabled = false;
static bool g_arm_dotprod_enabled = false;
static bool g_arm_pmull_enabled = false;
static bool g_color_output_enabled = true;
static xx_terminal_type_t g_terminal_type = XX_TERMINAL_TYPE_NONE;

/* Feature detection runs once even when several threads ask at the same
 * time (scanners run concurrently): 0 = not started, 1 = running, 2 = done.
 * The release store of 2 publishes the flags written before it.
 *
 * Every xx_is_*_enabled() call loads the state, often once per search call,
 * from many threads. On x86 and x64 an ordinary load already has acquire
 * ordering, so MSVC reads the volatile and adds a compiler barrier
 * (_ReadWriteBarrier keeps the flag reads after it); a locked
 * read-modify-write there would make every caller write the shared cache
 * line. Other MSVC targets (ARM64) keep the interlocked load, which is a
 * full barrier. */
#if defined(_MSC_VER)
#include <intrin.h>
static volatile long g_features_state = 0;
#if defined(_M_IX86) || (defined(_M_X64) && !defined(_M_ARM64EC))
static __forceinline long xx_features_load_acquire(void)
{
    long state = g_features_state;
    _ReadWriteBarrier();
    return state;
}
#define XX_FEATURES_LOAD() xx_features_load_acquire()
#else
#define XX_FEATURES_LOAD() _InterlockedOr(&g_features_state, 0)
#endif
#define XX_FEATURES_CLAIM() (_InterlockedCompareExchange(&g_features_state, 1, 0) == 0)
#define XX_FEATURES_DONE() _InterlockedExchange(&g_features_state, 2)
#elif defined(__GNUC__) || defined(__clang__)
static int g_features_state = 0;
#define XX_FEATURES_LOAD() __atomic_load_n(&g_features_state, __ATOMIC_ACQUIRE)
#define XX_FEATURES_CLAIM() \
    __extension__({ int expected_ = 0; __atomic_compare_exchange_n(&g_features_state, &expected_, 1, false, \
                                                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE); })
#define XX_FEATURES_DONE() __atomic_store_n(&g_features_state, 2, __ATOMIC_RELEASE)
#else
static int g_features_state = 0; /* no atomics: single-threaded first use */
#define XX_FEATURES_LOAD() g_features_state
#define XX_FEATURES_CLAIM() (g_features_state == 0 ? (g_features_state = 1, 1) : 0)
#define XX_FEATURES_DONE() (g_features_state = 2)
#endif

static void xx_global_init_features_once(void)
{
    if (XX_FEATURES_LOAD() == 2) return;
    if (!XX_FEATURES_CLAIM()) {
        /* Another thread is detecting; it takes microseconds. */
        while (XX_FEATURES_LOAD() != 2) {
        }
        return;
    }
    {
        g_sse2_detected = xx_global_platform_has_sse2();
        g_sse42_detected = xx_global_platform_has_sse42();
        g_avx2_detected = xx_global_platform_has_avx2();
        g_aes_detected = xx_global_platform_has_aes();
        g_sha_detected = xx_global_platform_has_sha();
        g_vpclmulqdq_detected = xx_global_platform_has_vpclmulqdq();
        g_neon_detected = xx_global_platform_has_neon();
        g_arm_crc32_detected = xx_global_platform_has_arm_crc32();
        g_arm_crypto_detected = xx_global_platform_has_arm_crypto();
        g_arm_dotprod_detected = xx_global_platform_has_arm_dotprod();
        g_arm_pmull_detected = xx_global_platform_has_arm_pmull();
        /* Default is on if feature exists in system */
        g_sse2_enabled = g_sse2_detected;
        g_sse42_enabled = g_sse42_detected;
        g_avx2_enabled = g_avx2_detected;
        g_aes_enabled = g_aes_detected;
        g_sha_enabled = g_sha_detected;
        g_vpclmulqdq_enabled = g_vpclmulqdq_detected;
        g_neon_enabled = g_neon_detected;
        g_arm_crc32_enabled = g_arm_crc32_detected;
        g_arm_crypto_enabled = g_arm_crypto_detected;
        g_arm_dotprod_enabled = g_arm_dotprod_detected;
        g_arm_pmull_enabled = g_arm_pmull_detected;
        g_terminal_type = xx_global_platform_detect_terminal_type(false);
        XX_FEATURES_DONE();
    }
}

void xx_set_color_output_enabled(bool enable)
{
    xx_global_init_features_once();
    g_color_output_enabled = enable;
}

bool xx_is_color_output_enabled(void)
{
    xx_global_init_features_once();
    return g_color_output_enabled;
}

xx_terminal_type_t xx_get_terminal_type(void)
{
    xx_global_init_features_once();
    return g_terminal_type;
}

void xx_set_terminal_type(xx_terminal_type_t type)
{
    xx_global_init_features_once();
    if (type != XX_TERMINAL_TYPE_ANSI && type != XX_TERMINAL_TYPE_WINDOWS) {
        type = XX_TERMINAL_TYPE_NONE;
    }
    g_terminal_type = type;
}

void xx_global_set_color_output_enabled(bool enable)
{
    xx_set_color_output_enabled(enable);
}

bool xx_global_is_color_output_enabled(void)
{
    return xx_is_color_output_enabled();
}

xx_terminal_type_t xx_global_get_terminal_type(void)
{
    return xx_get_terminal_type();
}

void xx_global_set_terminal_type(xx_terminal_type_t type)
{
    xx_set_terminal_type(type);
}

bool xx_has_sse2(void)
{
    xx_global_init_features_once();
    return g_sse2_detected;
}

bool xx_has_sse42(void)
{
    xx_global_init_features_once();
    return g_sse42_detected;
}

bool xx_has_avx2(void)
{
    xx_global_init_features_once();
    return g_avx2_detected;
}

bool xx_has_aes(void)
{
    xx_global_init_features_once();
    return g_aes_detected;
}

bool xx_has_sha(void)
{
    xx_global_init_features_once();
    return g_sha_detected;
}

bool xx_has_vpclmulqdq(void)
{
    xx_global_init_features_once();
    return g_vpclmulqdq_detected;
}

bool xx_has_neon(void)
{
    xx_global_init_features_once();
    return g_neon_detected;
}

bool xx_has_arm_crc32(void)
{
    xx_global_init_features_once();
    return g_arm_crc32_detected;
}

bool xx_has_arm_crypto(void)
{
    xx_global_init_features_once();
    return g_arm_crypto_detected;
}

bool xx_has_arm_dotprod(void)
{
    xx_global_init_features_once();
    return g_arm_dotprod_detected;
}

bool xx_has_arm_pmull(void)
{
    xx_global_init_features_once();
    return g_arm_pmull_detected;
}

void xx_set_sse2_enabled(bool enable)
{
    xx_global_init_features_once();
    g_sse2_enabled = enable && g_sse2_detected;
}

void xx_enable_sse2(bool enable)
{
    xx_set_sse2_enabled(enable);
}

bool xx_is_sse2_enabled(void)
{
    xx_global_init_features_once();
    return g_sse2_enabled && g_sse2_detected;
}

void xx_set_sse42_enabled(bool enable)
{
    xx_global_init_features_once();
    g_sse42_enabled = enable && g_sse42_detected;
}

void xx_enable_sse42(bool enable)
{
    xx_set_sse42_enabled(enable);
}

bool xx_is_sse42_enabled(void)
{
    xx_global_init_features_once();
    return g_sse42_enabled && g_sse42_detected;
}

void xx_set_avx2_enabled(bool enable)
{
    xx_global_init_features_once();
    g_avx2_enabled = enable && g_avx2_detected;
}

void xx_enable_avx2(bool enable)
{
    xx_set_avx2_enabled(enable);
}

bool xx_is_avx2_enabled(void)
{
    xx_global_init_features_once();
    return g_avx2_enabled && g_avx2_detected;
}

void xx_set_aes_enabled(bool enable)
{
    xx_global_init_features_once();
    g_aes_enabled = enable && g_aes_detected;
}

void xx_enable_aes(bool enable)
{
    xx_set_aes_enabled(enable);
}

bool xx_is_aes_enabled(void)
{
    xx_global_init_features_once();
    return g_aes_enabled && g_aes_detected;
}

void xx_set_sha_enabled(bool enable)
{
    xx_global_init_features_once();
    g_sha_enabled = enable && g_sha_detected;
}

void xx_enable_sha(bool enable)
{
    xx_set_sha_enabled(enable);
}

bool xx_is_sha_enabled(void)
{
    xx_global_init_features_once();
    return g_sha_enabled && g_sha_detected;
}

void xx_set_vpclmulqdq_enabled(bool enable)
{
    xx_global_init_features_once();
    g_vpclmulqdq_enabled = enable && g_vpclmulqdq_detected;
}

void xx_enable_vpclmulqdq(bool enable)
{
    xx_set_vpclmulqdq_enabled(enable);
}

bool xx_is_vpclmulqdq_enabled(void)
{
    xx_global_init_features_once();
    return g_vpclmulqdq_enabled && g_vpclmulqdq_detected;
}

void xx_set_neon_enabled(bool enable)
{
    xx_global_init_features_once();
    g_neon_enabled = enable && g_neon_detected;
}

void xx_enable_neon(bool enable)
{
    xx_set_neon_enabled(enable);
}

bool xx_is_neon_enabled(void)
{
    xx_global_init_features_once();
    return g_neon_enabled && g_neon_detected;
}

void xx_set_arm_crc32_enabled(bool enable)
{
    xx_global_init_features_once();
    g_arm_crc32_enabled = enable && g_arm_crc32_detected;
}

void xx_enable_arm_crc32(bool enable)
{
    xx_set_arm_crc32_enabled(enable);
}

bool xx_is_arm_crc32_enabled(void)
{
    xx_global_init_features_once();
    return g_arm_crc32_enabled && g_arm_crc32_detected;
}

void xx_set_arm_crypto_enabled(bool enable)
{
    xx_global_init_features_once();
    g_arm_crypto_enabled = enable && g_arm_crypto_detected;
}

void xx_enable_arm_crypto(bool enable)
{
    xx_set_arm_crypto_enabled(enable);
}

bool xx_is_arm_crypto_enabled(void)
{
    xx_global_init_features_once();
    return g_arm_crypto_enabled && g_arm_crypto_detected;
}

void xx_set_arm_dotprod_enabled(bool enable)
{
    xx_global_init_features_once();
    g_arm_dotprod_enabled = enable && g_arm_dotprod_detected;
}

void xx_set_arm_pmull_enabled(bool enable)
{
    xx_global_init_features_once();
    g_arm_pmull_enabled = enable && g_arm_pmull_detected;
}

void xx_enable_arm_dotprod(bool enable)
{
    xx_set_arm_dotprod_enabled(enable);
}

void xx_enable_arm_pmull(bool enable)
{
    xx_set_arm_pmull_enabled(enable);
}

bool xx_is_arm_dotprod_enabled(void)
{
    xx_global_init_features_once();
    return g_arm_dotprod_enabled && g_arm_dotprod_detected;
}

bool xx_is_arm_pmull_enabled(void)
{
    xx_global_init_features_once();
    return g_arm_pmull_enabled && g_arm_pmull_detected;
}

/* Aliases with xx_global prefix */
bool xx_global_has_sse2(void)
{
    return xx_has_sse2();
}

bool xx_global_has_sse42(void)
{
    return xx_has_sse42();
}

bool xx_global_has_avx2(void)
{
    return xx_has_avx2();
}

bool xx_global_has_aes(void)
{
    return xx_has_aes();
}

bool xx_global_has_sha(void)
{
    return xx_has_sha();
}

bool xx_global_has_vpclmulqdq(void)
{
    return xx_has_vpclmulqdq();
}

bool xx_global_has_neon(void)
{
    return xx_has_neon();
}

bool xx_global_has_arm_crc32(void)
{
    return xx_has_arm_crc32();
}

bool xx_global_has_arm_crypto(void)
{
    return xx_has_arm_crypto();
}

bool xx_global_has_arm_dotprod(void)
{
    return xx_has_arm_dotprod();
}

bool xx_global_has_arm_pmull(void)
{
    return xx_has_arm_pmull();
}

void xx_global_set_sse2_enabled(bool enable)
{
    xx_set_sse2_enabled(enable);
}

void xx_global_enable_sse2(bool enable)
{
    xx_enable_sse2(enable);
}

bool xx_global_is_sse2_enabled(void)
{
    return xx_is_sse2_enabled();
}

void xx_global_set_sse42_enabled(bool enable)
{
    xx_set_sse42_enabled(enable);
}

void xx_global_enable_sse42(bool enable)
{
    xx_enable_sse42(enable);
}

bool xx_global_is_sse42_enabled(void)
{
    return xx_is_sse42_enabled();
}

void xx_global_set_avx2_enabled(bool enable)
{
    xx_set_avx2_enabled(enable);
}

void xx_global_enable_avx2(bool enable)
{
    xx_enable_avx2(enable);
}

bool xx_global_is_avx2_enabled(void)
{
    return xx_is_avx2_enabled();
}

void xx_global_set_aes_enabled(bool enable)
{
    xx_set_aes_enabled(enable);
}

void xx_global_enable_aes(bool enable)
{
    xx_enable_aes(enable);
}

bool xx_global_is_aes_enabled(void)
{
    return xx_is_aes_enabled();
}

void xx_global_set_sha_enabled(bool enable)
{
    xx_set_sha_enabled(enable);
}

void xx_global_enable_sha(bool enable)
{
    xx_enable_sha(enable);
}

bool xx_global_is_sha_enabled(void)
{
    return xx_is_sha_enabled();
}

void xx_global_set_vpclmulqdq_enabled(bool enable)
{
    xx_set_vpclmulqdq_enabled(enable);
}

void xx_global_enable_vpclmulqdq(bool enable)
{
    xx_enable_vpclmulqdq(enable);
}

bool xx_global_is_vpclmulqdq_enabled(void)
{
    return xx_is_vpclmulqdq_enabled();
}

void xx_global_set_neon_enabled(bool enable)
{
    xx_set_neon_enabled(enable);
}

void xx_global_enable_neon(bool enable)
{
    xx_enable_neon(enable);
}

bool xx_global_is_neon_enabled(void)
{
    return xx_is_neon_enabled();
}

void xx_global_set_arm_crc32_enabled(bool enable)
{
    xx_set_arm_crc32_enabled(enable);
}

void xx_global_enable_arm_crc32(bool enable)
{
    xx_enable_arm_crc32(enable);
}

bool xx_global_is_arm_crc32_enabled(void)
{
    return xx_is_arm_crc32_enabled();
}

void xx_global_set_arm_crypto_enabled(bool enable)
{
    xx_set_arm_crypto_enabled(enable);
}

void xx_global_enable_arm_crypto(bool enable)
{
    xx_enable_arm_crypto(enable);
}

bool xx_global_is_arm_crypto_enabled(void)
{
    return xx_is_arm_crypto_enabled();
}

void xx_global_set_arm_dotprod_enabled(bool enable)
{
    xx_set_arm_dotprod_enabled(enable);
}

void xx_global_set_arm_pmull_enabled(bool enable)
{
    xx_set_arm_pmull_enabled(enable);
}

void xx_global_enable_arm_dotprod(bool enable)
{
    xx_enable_arm_dotprod(enable);
}

void xx_global_enable_arm_pmull(bool enable)
{
    xx_enable_arm_pmull(enable);
}

bool xx_global_is_arm_dotprod_enabled(void)
{
    return xx_is_arm_dotprod_enabled();
}

bool xx_global_is_arm_pmull_enabled(void)
{
    return xx_is_arm_pmull_enabled();
}
