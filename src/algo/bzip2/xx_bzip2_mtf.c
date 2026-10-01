/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_bzip2_mtf.h"
#include "platforms/xx_bzip2_mtf_platform.h"
#include "xxfclib/memory/xx_memory.h"

static bool xx_bzip2_mtf_scalar(const uint8_t *src, size_t count, int *output,
                                uint8_t alphabet[256], unsigned used) {
    size_t at;
    for (at = 0; at < count; ++at) {
        uint8_t symbol = src[at];
        unsigned pos, end;
        if (symbol == alphabet[0]) { output[at] = 0; continue; }
        if (used > 1 && symbol == alphabet[1]) {
            alphabet[1] = alphabet[0]; alphabet[0] = symbol;
            output[at] = 1; continue;
        }
        if (used > 2 && symbol == alphabet[2]) {
            alphabet[2] = alphabet[1]; alphabet[1] = alphabet[0]; alphabet[0] = symbol;
            output[at] = 2; continue;
        }
        if (used > 3 && symbol == alphabet[3]) {
            alphabet[3] = alphabet[2]; alphabet[2] = alphabet[1];
            alphabet[1] = alphabet[0]; alphabet[0] = symbol;
            output[at] = 3; continue;
        }
        pos = 4;
        while (pos < used && alphabet[pos] != symbol) ++pos;
        if (pos >= used) return false;
        output[at] = (int)pos;
        for (end = pos; end != 0; --end) alphabet[end] = alphabet[end - 1];
        alphabet[0] = symbol;
    }
    return true;
}

#ifdef XX_BZIP2_MTF_X86
XX_BZIP2_MTF_TARGET_SSE2
static XX_BZIP2_MTF_NOINLINE bool xx_bzip2_mtf_sse2(const uint8_t *src,
                                                   size_t count, int *output,
                                                   uint8_t alphabet[256],
                                                   unsigned used) {
    size_t at;
    for (at = 0; at < count; ++at) {
        uint8_t symbol = src[at];
        unsigned pos, end;
        if (symbol == alphabet[0]) { output[at] = 0; continue; }
        if (used > 1 && symbol == alphabet[1]) {
            alphabet[1] = alphabet[0]; alphabet[0] = symbol;
            output[at] = 1; continue;
        }
        if (used > 2 && symbol == alphabet[2]) {
            alphabet[2] = alphabet[1]; alphabet[1] = alphabet[0]; alphabet[0] = symbol;
            output[at] = 2; continue;
        }
        if (used > 3 && symbol == alphabet[3]) {
            alphabet[3] = alphabet[2]; alphabet[2] = alphabet[1];
            alphabet[1] = alphabet[0]; alphabet[0] = symbol;
            output[at] = 3; continue;
        }

        /* Every loaded byte belongs to the initialized alphabet. In
         * particular the final short group is searched scalarly. */
        pos = 0;
        if (used >= 16) {
            __m128i wanted = _mm_set1_epi8((char)symbol);
            while (used - pos >= 16) {
                __m128i values = _mm_loadu_si128(
                    (const __m128i *)(const void *)(alphabet + pos));
                unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(values, wanted));
                if (mask != 0) {
#if defined(_MSC_VER)
                    unsigned long bit;
                    _BitScanForward(&bit, mask);
                    pos += (unsigned)bit;
#elif defined(__GNUC__) || defined(__clang__)
                    pos += (unsigned)__builtin_ctz(mask);
#else
                    while ((mask & 1U) == 0) { ++pos; mask >>= 1; }
#endif
                    break;
                }
                pos += 16;
            }
        } else {
            /* The first four positions were already checked. */
            pos = 4;
        }
        while (pos < used && alphabet[pos] != symbol) ++pos;
        if (pos >= used) return false;
        output[at] = (int)pos;

        /* Shift from the back so overlapping source/destination spans remain
         * intact. Each vector is fully loaded before its one-byte-offset
         * store, and no store passes the matched symbol's position. */
        end = pos;
        while (end >= 16) {
            __m128i values = _mm_loadu_si128(
                (const __m128i *)(const void *)(alphabet + end - 16));
            _mm_storeu_si128((__m128i *)(void *)(alphabet + end - 15), values);
            end -= 16;
        }
        while (end != 0) { alphabet[end] = alphabet[end - 1]; --end; }
        alphabet[0] = symbol;
    }
    return true;
}

static unsigned xx_bzip2_mtf_detect_capabilities(void) {
    unsigned result = 1U << XX_BZIP2_MTF_SCALAR;
    unsigned edx = 0;
#if defined(_MSC_VER)
    int registers[4];
    __cpuid(registers, 0);
    if ((unsigned)registers[0] >= 1) { __cpuidex(registers, 1, 0); edx = (unsigned)registers[3]; }
#elif defined(__GNUC__) || defined(__clang__)
    unsigned eax, ebx, ecx;
    if (__get_cpuid_max(0, NULL) >= 1) __cpuid_count(1, 0, eax, ebx, ecx, edx);
#endif
    if ((edx & (1U << 26)) != 0) result |= 1U << XX_BZIP2_MTF_SSE2;
    return result;
}
#endif /* XX_BZIP2_MTF_X86 */

unsigned xx_bzip2_mtf_backend_capabilities(void) {
#ifdef XX_BZIP2_MTF_X86
#if defined(_MSC_VER)
    static volatile long cached = 0;
    long value = _InterlockedCompareExchange(&cached, 0, 0);
    if (value == 0) {
        value = (long)xx_bzip2_mtf_detect_capabilities();
        _InterlockedCompareExchange(&cached, value, 0);
    }
    return (unsigned)value;
#elif defined(__GNUC__) || defined(__clang__)
    static unsigned cached = 0;
    unsigned value = __atomic_load_n(&cached, __ATOMIC_RELAXED);
    if (value == 0) {
        value = xx_bzip2_mtf_detect_capabilities();
        __atomic_store_n(&cached, value, __ATOMIC_RELAXED);
    }
    return value;
#else
    return xx_bzip2_mtf_detect_capabilities();
#endif
#else
    return 1U << XX_BZIP2_MTF_SCALAR;
#endif
}

int xx_bzip2_mtf_selected_backend(void) {
    if (xx_bzip2_mtf_backend_capabilities() & (1U << XX_BZIP2_MTF_SSE2)) return XX_BZIP2_MTF_SSE2;
    return XX_BZIP2_MTF_SCALAR;
}

const char *xx_bzip2_mtf_backend_name(int backend) {
    switch (backend) {
    case XX_BZIP2_MTF_AUTO: return "auto";
    case XX_BZIP2_MTF_SCALAR: return "scalar";
    case XX_BZIP2_MTF_SSE2: return "sse2";
    default: return "unsupported";
    }
}

bool xx_bzip2_mtf_encode_backend(const uint8_t *src, size_t count, int *output,
                                  const uint8_t in_use[256], int backend) {
    uint8_t alphabet[256];
    unsigned used = 0, value;
    bool ok;
    if (!in_use || (count != 0 && (!src || !output)) || count > SIZE_MAX / sizeof(int)) return false;
    if (backend == XX_BZIP2_MTF_AUTO) backend = xx_bzip2_mtf_selected_backend();
    if (backend < XX_BZIP2_MTF_SCALAR || backend > XX_BZIP2_MTF_SSE2 ||
        !(xx_bzip2_mtf_backend_capabilities() & (1U << backend))) return false;
    if (count == 0) return true;
    for (value = 0; value < 256; ++value) {
        if (in_use[value]) alphabet[used++] = (uint8_t)value;
    }
    if (used == 0) return false;
#ifdef XX_BZIP2_MTF_X86
    if (backend == XX_BZIP2_MTF_SSE2) ok = xx_bzip2_mtf_sse2(src, count, output, alphabet, used);
    else
#endif
    ok = xx_bzip2_mtf_scalar(src, count, output, alphabet, used);
    xx_mem_zero(alphabet, sizeof(alphabet));
    return ok;
}

bool xx_bzip2_mtf_encode(const uint8_t *src, size_t count, int *output,
                          const uint8_t in_use[256]) {
    return xx_bzip2_mtf_encode_backend(src, count, output, in_use, XX_BZIP2_MTF_AUTO);
}
