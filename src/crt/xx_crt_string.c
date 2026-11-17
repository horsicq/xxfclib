/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_crt_string.c
 * @brief memcpy, memmove, memset, memcmp, memchr and strlen for the
 *        standard-name C runtime layer.
 *
 * These back the compiler's own calls (structure copies, zero
 * initialisation) as well as every explicit call, so short sizes matter as
 * much as long ones: up to 64 bytes are moved with a fixed number of
 * overlapping loads and stores, longer ranges with an SSE2 loop, and ranges
 * of 512 bytes or more go to xxfclib's AVX2 memory backends when the CPU and
 * the global switch allow. memchr and strlen read only aligned 16-byte
 * blocks that contain at least one byte of the range, so they never touch a
 * page the range does not touch. memcmp answers -1, 0 or 1 like UCRT's.
 *
 * No loop here is a plain byte loop the optimiser could turn back into a
 * call to memcpy or memset, which inside the layer that defines those
 * functions would recurse.
 */

#include "xxfclib/crt/xx_crt.h"
#include "xxfclib/global/xx_global.h"
#include "../memory/platforms/xx_memory_platform.h"

#include <stdint.h>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#define XX_CRT_STRING_SSE2 1
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#endif

#if defined(_MSC_VER)
#pragma pack(push, 1)
typedef struct xx_crt_u32_s {
    uint32_t value;
} xx_crt_u32;
typedef struct xx_crt_u16_s {
    uint16_t value;
} xx_crt_u16;
#pragma pack(pop)
#elif defined(__GNUC__) || defined(__clang__)
typedef struct __attribute__((packed, may_alias)) xx_crt_u32_s {
    uint32_t value;
} xx_crt_u32;
typedef struct __attribute__((packed, may_alias)) xx_crt_u16_s {
    uint16_t value;
} xx_crt_u16;
#else
typedef struct xx_crt_u32_s {
    uint32_t value;
} xx_crt_u32;
typedef struct xx_crt_u16_s {
    uint16_t value;
} xx_crt_u16;
#endif

static uint32_t xx_crt_load32(const uint8_t *p)
{
    return ((const xx_crt_u32 *)(const void *)p)->value;
}
static void xx_crt_store32(uint8_t *p, uint32_t v)
{
    ((volatile xx_crt_u32 *)(void *)p)->value = v;
}
static uint16_t xx_crt_load16(const uint8_t *p)
{
    return ((const xx_crt_u16 *)(const void *)p)->value;
}
static void xx_crt_store16(uint8_t *p, uint16_t v)
{
    ((volatile xx_crt_u16 *)(void *)p)->value = v;
}

static unsigned xx_crt_ctz32(uint32_t nValue)
{
#if defined(_MSC_VER)
    unsigned long nIndex;
    _BitScanForward(&nIndex, nValue);
    return (unsigned)nIndex;
#elif defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctz(nValue);
#else
    unsigned n = 0;
    while (!(nValue & 1u)) {
        nValue >>= 1;
        ++n;
    }
    return n;
#endif
}

static unsigned xx_crt_ctz64(uint64_t nValue)
{
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned long nIndex;
    _BitScanForward64(&nIndex, nValue);
    return (unsigned)nIndex;
#elif defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(nValue);
#else
    unsigned n = 0;
    while (!(nValue & 1u)) {
        nValue >>= 1;
        ++n;
    }
    return n;
#endif
}

/* Short copies of 0..16 bytes with overlapping word moves; all loads come
 * before the stores, so an overlapping (memmove) range is fine too. */
static void xx_crt_copy_small(uint8_t *d, const uint8_t *s, size_t n)
{
    if (n >= 8) {
        uint64_t a = xx_memory_load_word(s);
        uint64_t b = xx_memory_load_word(s + n - 8);
        xx_memory_store_word(d, a);
        xx_memory_store_word(d + n - 8, b);
    } else if (n >= 4) {
        uint32_t a = xx_crt_load32(s);
        uint32_t b = xx_crt_load32(s + n - 4);
        xx_crt_store32(d, a);
        xx_crt_store32(d + n - 4, b);
    } else if (n >= 2) {
        uint16_t a = xx_crt_load16(s);
        uint16_t b = xx_crt_load16(s + n - 2);
        xx_crt_store16(d, a);
        xx_crt_store16(d + n - 2, b);
    } else if (n == 1) {
        *(volatile uint8_t *)d = *s;
    }
}

static int xx_crt_avx2_large(void)
{
#if defined(XX_CRT_STRING_SSE2)
    return xx_is_avx2_enabled() ? 1 : 0;
#else
    return 0;
#endif
}

void *xx_crt_memcpy(void *pDestination, const void *pSource, size_t nSize)
{
    uint8_t *d = (uint8_t *)pDestination;
    const uint8_t *s = (const uint8_t *)pSource;
#if defined(XX_CRT_STRING_SSE2)
    if (nSize <= 16) {
        xx_crt_copy_small(d, s, nSize);
        return pDestination;
    }
    if (nSize <= 32) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 16));
        _mm_storeu_si128((__m128i *)(void *)d, a);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 16), b);
        return pDestination;
    }
    if (nSize <= 64) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(s + 16));
        __m128i c = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 32));
        __m128i e = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 16));
        _mm_storeu_si128((__m128i *)(void *)d, a);
        _mm_storeu_si128((__m128i *)(void *)(d + 16), b);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 32), c);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 16), e);
        return pDestination;
    }
    if (nSize >= 512 && xx_crt_avx2_large()) {
        xx_memory_copy_avx2(d, s, nSize);
        return pDestination;
    }
    {
        __m128i t0 = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 64));
        __m128i t1 = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 48));
        __m128i t2 = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 32));
        __m128i t3 = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 16));
        size_t i;
        for (i = 0; i + 64 <= nSize; i += 64) {
            __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(s + i));
            __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(s + i + 16));
            __m128i c = _mm_loadu_si128((const __m128i *)(const void *)(s + i + 32));
            __m128i e = _mm_loadu_si128((const __m128i *)(const void *)(s + i + 48));
            _mm_storeu_si128((__m128i *)(void *)(d + i), a);
            _mm_storeu_si128((__m128i *)(void *)(d + i + 16), b);
            _mm_storeu_si128((__m128i *)(void *)(d + i + 32), c);
            _mm_storeu_si128((__m128i *)(void *)(d + i + 48), e);
        }
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 64), t0);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 48), t1);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 32), t2);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 16), t3);
    }
    return pDestination;
#else
    if (nSize <= 16) {
        xx_crt_copy_small(d, s, nSize);
        return pDestination;
    }
    xx_memory_copy_scalar(d, s, nSize);
    return pDestination;
#endif
}

void *xx_crt_memmove(void *pDestination, const void *pSource, size_t nSize)
{
    uint8_t *d = (uint8_t *)pDestination;
    const uint8_t *s = (const uint8_t *)pSource;
    if ((size_t)((uintptr_t)d - (uintptr_t)s) >= nSize) {
        /* the destination does not start inside the source: a forward copy is safe */
        if ((size_t)((uintptr_t)s - (uintptr_t)d) >= nSize) return xx_crt_memcpy(pDestination, pSource, nSize);
    }
    if (nSize <= 16) {
        xx_crt_copy_small(d, s, nSize);
        return pDestination;
    }
#if defined(XX_CRT_STRING_SSE2)
    /* every load before the first store: any overlap is fine */
    if (nSize <= 32) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 16));
        _mm_storeu_si128((__m128i *)(void *)d, a);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 16), b);
        return pDestination;
    }
    if (nSize <= 64) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(s + 16));
        __m128i c = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 32));
        __m128i e = _mm_loadu_si128((const __m128i *)(const void *)(s + nSize - 16));
        _mm_storeu_si128((__m128i *)(void *)d, a);
        _mm_storeu_si128((__m128i *)(void *)(d + 16), b);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 32), c);
        _mm_storeu_si128((__m128i *)(void *)(d + nSize - 16), e);
        return pDestination;
    }
    if (nSize >= 512 && xx_crt_avx2_large()) {
        xx_memory_move_avx2(d, s, nSize);
        return pDestination;
    }
    xx_memory_move_sse2(d, s, nSize);
#else
    xx_memory_move_scalar(d, s, nSize);
#endif
    return pDestination;
}

void *xx_crt_memset(void *pDestination, int nValue, size_t nSize)
{
    uint8_t *d = (uint8_t *)pDestination;
    uint64_t nPattern = (uint64_t)(uint8_t)nValue * 0x0101010101010101ull;
    if (nSize <= 16) {
        if (nSize >= 8) {
            xx_memory_store_word(d, nPattern);
            xx_memory_store_word(d + nSize - 8, nPattern);
        } else if (nSize >= 4) {
            xx_crt_store32(d, (uint32_t)nPattern);
            xx_crt_store32(d + nSize - 4, (uint32_t)nPattern);
        } else if (nSize >= 2) {
            xx_crt_store16(d, (uint16_t)nPattern);
            xx_crt_store16(d + nSize - 2, (uint16_t)nPattern);
        } else if (nSize == 1) {
            *(volatile uint8_t *)d = (uint8_t)nValue;
        }
        return pDestination;
    }
#if defined(XX_CRT_STRING_SSE2)
    {
        __m128i v = _mm_set1_epi8((char)nValue);
        if (nSize <= 32) {
            _mm_storeu_si128((__m128i *)(void *)d, v);
            _mm_storeu_si128((__m128i *)(void *)(d + nSize - 16), v);
            return pDestination;
        }
        if (nSize <= 64) {
            _mm_storeu_si128((__m128i *)(void *)d, v);
            _mm_storeu_si128((__m128i *)(void *)(d + 16), v);
            _mm_storeu_si128((__m128i *)(void *)(d + nSize - 32), v);
            _mm_storeu_si128((__m128i *)(void *)(d + nSize - 16), v);
            return pDestination;
        }
        if (nSize >= 512 && xx_crt_avx2_large()) {
            xx_memory_set_avx2(d, (uint8_t)nValue, nSize);
            return pDestination;
        }
        {
            size_t i;
            for (i = 0; i + 64 <= nSize; i += 64) {
                _mm_storeu_si128((__m128i *)(void *)(d + i), v);
                _mm_storeu_si128((__m128i *)(void *)(d + i + 16), v);
                _mm_storeu_si128((__m128i *)(void *)(d + i + 32), v);
                _mm_storeu_si128((__m128i *)(void *)(d + i + 48), v);
            }
            _mm_storeu_si128((__m128i *)(void *)(d + nSize - 64), v);
            _mm_storeu_si128((__m128i *)(void *)(d + nSize - 48), v);
            _mm_storeu_si128((__m128i *)(void *)(d + nSize - 32), v);
            _mm_storeu_si128((__m128i *)(void *)(d + nSize - 16), v);
        }
    }
#else
    xx_memory_set_scalar(d, (uint8_t)nValue, nSize);
#endif
    return pDestination;
}

static int xx_crt_byte_order(uint8_t nLeft, uint8_t nRight)
{
    return nLeft < nRight ? -1 : 1;
}

int xx_crt_memcmp(const void *pLeft, const void *pRight, size_t nSize)
{
    const uint8_t *a = (const uint8_t *)pLeft;
    const uint8_t *b = (const uint8_t *)pRight;
    size_t i = 0;
#if defined(XX_CRT_STRING_SSE2)
    for (; i + 16 <= nSize; i += 16) {
        __m128i x = _mm_loadu_si128((const __m128i *)(const void *)(a + i));
        __m128i y = _mm_loadu_si128((const __m128i *)(const void *)(b + i));
        unsigned nMask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(x, y)) ^ 0xFFFFu;
        if (nMask) {
            unsigned nAt = xx_crt_ctz32(nMask);
            return xx_crt_byte_order(a[i + nAt], b[i + nAt]);
        }
    }
#endif
    for (; i + 8 <= nSize; i += 8) {
        uint64_t x = xx_memory_load_word(a + i);
        uint64_t y = xx_memory_load_word(b + i);
        if (x != y) {
            unsigned nAt = xx_crt_ctz64(x ^ y) / 8;
            return xx_crt_byte_order(a[i + nAt], b[i + nAt]);
        }
    }
    for (; i < nSize; ++i) {
        if (a[i] != b[i]) return xx_crt_byte_order(a[i], b[i]);
    }
    return 0;
}

void *xx_crt_memchr(const void *pMemory, int nChar, size_t nSize)
{
    const uint8_t *p = (const uint8_t *)pMemory;
    uint8_t nByte = (uint8_t)nChar;
    if (nSize == 0) return NULL;
#if defined(XX_CRT_STRING_SSE2)
    {
        const uint8_t *pEnd = p + nSize;
        const uint8_t *pBlock = (const uint8_t *)((uintptr_t)p & ~(uintptr_t)15);
        __m128i v = _mm_set1_epi8((char)nByte);
        unsigned nMask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_load_si128((const __m128i *)(const void *)pBlock), v));
        nMask &= 0xFFFFu << (unsigned)(p - pBlock);
        for (;;) {
            if (nMask) {
                const uint8_t *pHit = pBlock + xx_crt_ctz32(nMask);
                return pHit < pEnd ? (void *)pHit : NULL;
            }
            pBlock += 16;
            if (pBlock >= pEnd) return NULL;
            if (pEnd - pBlock >= 64) {
                __m128i x0 = _mm_cmpeq_epi8(_mm_load_si128((const __m128i *)(const void *)pBlock), v);
                __m128i x1 = _mm_cmpeq_epi8(_mm_load_si128((const __m128i *)(const void *)(pBlock + 16)), v);
                __m128i x2 = _mm_cmpeq_epi8(_mm_load_si128((const __m128i *)(const void *)(pBlock + 32)), v);
                __m128i x3 = _mm_cmpeq_epi8(_mm_load_si128((const __m128i *)(const void *)(pBlock + 48)), v);
                if (!_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(x0, x1), _mm_or_si128(x2, x3)))) {
                    pBlock += 48;
                    nMask = 0;
                    continue;
                }
            }
            nMask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_load_si128((const __m128i *)(const void *)pBlock), v));
        }
    }
#else
    {
        size_t i;
        for (i = 0; i < nSize; ++i) {
            if (p[i] == nByte) return (void *)(p + i);
        }
        return NULL;
    }
#endif
}

char *xx_crt_strchr(const char *pString, int nChar)
{
#if defined(XX_CRT_STRING_SSE2)
    /* aligned 16-byte blocks: the first block holding the character or the
     * terminator decides; reads never leave the pages the string touches */
    const uint8_t *p = (const uint8_t *)pString;
    const uint8_t *pBlock = (const uint8_t *)((uintptr_t)p & ~(uintptr_t)15);
    __m128i z = _mm_setzero_si128();
    __m128i v = _mm_set1_epi8((char)nChar);
    __m128i x = _mm_load_si128((const __m128i *)(const void *)pBlock);
    unsigned nMask = (unsigned)_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(x, z), _mm_cmpeq_epi8(x, v)));
    nMask &= 0xFFFFu << (unsigned)(p - pBlock);
    while (!nMask) {
        pBlock += 16;
        x = _mm_load_si128((const __m128i *)(const void *)pBlock);
        nMask = (unsigned)_mm_movemask_epi8(_mm_or_si128(_mm_cmpeq_epi8(x, z), _mm_cmpeq_epi8(x, v)));
    }
    p = pBlock + xx_crt_ctz32(nMask);
    return *p == (uint8_t)nChar ? (char *)p : NULL;
#else
    char c = (char)nChar;
    for (;; ++pString) {
        if (*pString == c) return (char *)pString;
        if (!*pString) return NULL;
    }
#endif
}

size_t xx_crt_strlen(const char *pString)
{
#if defined(XX_CRT_STRING_SSE2)
    const uint8_t *p = (const uint8_t *)pString;
    const uint8_t *pBlock = (const uint8_t *)((uintptr_t)p & ~(uintptr_t)15);
    __m128i z = _mm_setzero_si128();
    unsigned nMask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_load_si128((const __m128i *)(const void *)pBlock), z));
    nMask &= 0xFFFFu << (unsigned)(p - pBlock);
    while (!nMask) {
        pBlock += 16;
        nMask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_load_si128((const __m128i *)(const void *)pBlock), z));
    }
    return (size_t)(pBlock + xx_crt_ctz32(nMask) - p);
#else
    const char *p = pString;
    while (*p) ++p;
    return (size_t)(p - pString);
#endif
}
