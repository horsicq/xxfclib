/* Copyright (c) 2026 hors<horsicq@gmail.com>; SPDX-License-Identifier: MIT.
 * FEAD adapter of the library's xx_7zip_branch.c BCJ2 decoder: fixed zero IP,
 * cancellation/progress every64KiB; no shared codec source is modified. */
#include "fead_bcj2.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>
#include "xxfclib/data/xx_data.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

static inline size_t fead_bcj2_scan_and_copy(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    const __m128i v_e8 = _mm_set1_epi8((char)-24); /* 0xE8 */
    const __m128i v_fe = _mm_set1_epi8((char)-2);  /* 0xFE */
    const __m128i v_0f = _mm_set1_epi8((char)0x0F);

    while (i + 32 <= len) {
        __m128i d0 = _mm_loadu_si128((const __m128i *)(const void *)(src + i));
        __m128i m0 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d0, v_fe), v_e8), _mm_cmpeq_epi8(d0, v_0f));
        int mask0 = _mm_movemask_epi8(m0);
        if (mask0 != 0) {
            _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask0);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask0);
#endif
            return i + (size_t)bit_idx;
        }

        __m128i d1 = _mm_loadu_si128((const __m128i *)(const void *)(src + i + 16));
        __m128i m1 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d1, v_fe), v_e8), _mm_cmpeq_epi8(d1, v_0f));
        int mask1 = _mm_movemask_epi8(m1);
        _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
        _mm_storeu_si128((__m128i *)(void *)(dst + i + 16), d1);
        if (mask1 != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask1);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask1);
#endif
            return i + 16 + (size_t)bit_idx;
        }
        i += 32;
    }
    while (i + 16 <= len) {
        __m128i d0 = _mm_loadu_si128((const __m128i *)(const void *)(src + i));
        __m128i m0 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d0, v_fe), v_e8), _mm_cmpeq_epi8(d0, v_0f));
        int mask = _mm_movemask_epi8(m0);
        _mm_storeu_si128((__m128i *)(void *)(dst + i), d0);
        if (mask != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask);
#endif
            return i + (size_t)bit_idx;
        }
        i += 16;
    }
    while (i < len) {
        uint8_t b = src[i];
        if (b == 0xE8U || b == 0xE9U || b == 0x0FU) return i;
        dst[i] = b;
        i++;
    }
    return len;
}
#else
static inline size_t fead_bcj2_scan_and_copy(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    while (i < len) {
        uint8_t b = src[i];
        if (b == 0xE8U || b == 0xE9U || b == 0x0FU) return i;
        dst[i] = b;
        i++;
    }
    return len;
}
#endif

bool fead_bcj2_decode(const uint8_t *const inputs[4], const size_t sizes[4], uint8_t *output, size_t output_size, xx_pd_struct *pd)
{
    const uint8_t *main_data, *call_data, *jump_data, *range_data;
    size_t main_size, call_size, jump_size, range_size;
    size_t main_pos = 0, call_pos = 0, jump_pos = 0, range_pos = 0;
    size_t produced = 0, checkpoint = 0, i;
    uint32_t range = UINT32_MAX, code = 0, ip = 0;
    uint16_t probabilities[258];
    uint8_t previous = 0, first;
    int level;
    bool ok = false;

    if (!inputs || !sizes || (!output && output_size) || (!inputs[0] && sizes[0]) || (!inputs[1] && sizes[1]) || (!inputs[2] && sizes[2]) || (!inputs[3] && sizes[3]) ||
        sizes[3] < 5 || (sizes[1] & 3U) || (sizes[2] & 3U) || xx_pd_is_stopped(pd))
        return false;

    main_data = inputs[0];
    main_size = sizes[0];
    call_data = inputs[1];
    call_size = sizes[1];
    jump_data = inputs[2];
    jump_size = sizes[2];
    range_data = inputs[3];
    range_size = sizes[3];

    for (i = 0; i < 258; ++i) probabilities[i] = 1024;
    first = range_data[range_pos++];
    if (first != 0) return false;
    for (i = 0; i < 4; ++i) {
        code = (code << 8) | range_data[range_pos++];
    }
    if (code == UINT32_MAX) return false;

    level = xx_pd_enter_level(pd, output_size, "FEAD BCJ2 restore");
    while (main_pos < main_size) {
        uint8_t opcode;
        bool branch;
        if (main_pos >= checkpoint) {
            if (xx_pd_is_stopped(pd)) goto done;
            xx_pd_set_current(pd, level, produced);
            checkpoint = main_pos > SIZE_MAX - 65536U ? SIZE_MAX : main_pos + 65536U;
        }

        if (previous != 0x0FU) {
            size_t avail_in = main_size - main_pos;
            size_t avail_out = output_size - produced;
            size_t to_checkpoint = checkpoint > main_pos ? (checkpoint - main_pos) : 0;
            size_t max_run = avail_in < avail_out ? avail_in : avail_out;
            if (to_checkpoint > 0 && max_run > to_checkpoint) max_run = to_checkpoint;

            if (max_run > 0) {
                size_t copied = fead_bcj2_scan_and_copy(output + produced, main_data + main_pos, max_run);
                if (copied) {
                    produced += copied;
                    main_pos += copied;
                    ip += (uint32_t)copied;
                    previous = main_data[main_pos - 1];
                    if (main_pos >= main_size || produced >= output_size || main_pos >= checkpoint) continue;
                }
            }
        }

        opcode = main_data[main_pos++];
        branch = opcode == 0xe8U || opcode == 0xe9U || (previous == 0x0fU && (opcode & 0xf0U) == 0x80U);
        if (produced >= output_size) goto done;
        output[produced++] = opcode;
        ++ip;

        if (branch) {
            uint16_t *probability = opcode == 0xe8U ? &probabilities[2U + previous] : &probabilities[opcode == 0xe9U ? 1U : 0U];
            uint32_t bound;
            if (range < (UINT32_C(1) << 24)) {
                if (range_pos >= range_size) goto done;
                range <<= 8;
                code = (code << 8) | range_data[range_pos++];
            }
            bound = (range >> 11) * (uint32_t)(*probability);
            if (code < bound) {
                range = bound;
                *probability = (uint16_t)(*probability + ((2048U - *probability) >> 5));
                previous = opcode;
            } else {
                range -= bound;
                code -= bound;
                *probability = (uint16_t)(*probability - (*probability >> 5));

                const uint8_t *target = (opcode == 0xe8U) ? (call_data + call_pos) : (jump_data + jump_pos);
                size_t *position = (opcode == 0xe8U) ? &call_pos : &jump_pos;
                size_t target_size = (opcode == 0xe8U) ? call_size : jump_size;

                if (*position > target_size || target_size - *position < 4 || output_size - produced < 4) goto done;

#if defined(_M_IX86) || defined(_M_X64)
                uint32_t value = _byteswap_ulong(*(const uint32_t *)(const void *)target);
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                uint32_t value = __builtin_bswap32(*(const uint32_t *)(const void *)target);
#else
                uint32_t value = ((uint32_t)target[0] << 24U) | ((uint32_t)target[1] << 16U) | ((uint32_t)target[2] << 8U) | (uint32_t)target[3];
#endif
                *position += 4;
                ip += 4;
                value -= ip;

#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                *(uint32_t *)(void *)(output + produced) = value;
                produced += 4;
                previous = (uint8_t)(value >> 24);
#else
                output[produced++] = (uint8_t)value;
                output[produced++] = (uint8_t)(value >> 8);
                output[produced++] = (uint8_t)(value >> 16);
                output[produced++] = (uint8_t)(value >> 24);
                previous = (uint8_t)(value >> 24);
#endif
                continue;
            }
        } else {
            previous = opcode;
        }
    }
    ok = produced == output_size && call_pos == call_size && jump_pos == jump_size && code == 0 && !xx_pd_is_stopped(pd);
    if (ok) xx_pd_set_current(pd, level, produced);
done:
    xx_pd_leave_level(pd, level);
    return ok && !xx_pd_is_stopped(pd);
}
