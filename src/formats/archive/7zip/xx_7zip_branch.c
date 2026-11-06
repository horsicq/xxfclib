/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_7zip_branch.h"
#include "xx_7zip_defs.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/data/xx_data.h"
#include <string.h>

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#include <stdlib.h>
#endif
#endif

static inline size_t xx_bcj_scan_candidates(const uint8_t *data, size_t candidate, size_t limit)
{
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    const __m128i v_fe = _mm_set1_epi8((char)-2);  /* 0xFE */
    const __m128i v_e8 = _mm_set1_epi8((char)-24); /* 0xE8 */

    while (candidate + 32 <= limit) {
        __m128i d0 = _mm_loadu_si128((const __m128i *)(const void *)(data + candidate));
        __m128i m0 = _mm_cmpeq_epi8(_mm_and_si128(d0, v_fe), v_e8);
        int mask0 = _mm_movemask_epi8(m0);
        if (mask0 != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask0);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask0);
#endif
            return candidate + (size_t)bit_idx;
        }

        __m128i d1 = _mm_loadu_si128((const __m128i *)(const void *)(data + candidate + 16));
        __m128i m1 = _mm_cmpeq_epi8(_mm_and_si128(d1, v_fe), v_e8);
        int mask1 = _mm_movemask_epi8(m1);
        if (mask1 != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask1);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask1);
#endif
            return candidate + 16 + (size_t)bit_idx;
        }
        candidate += 32;
    }
    while (candidate + 16 <= limit) {
        __m128i d0 = _mm_loadu_si128((const __m128i *)(const void *)(data + candidate));
        __m128i m0 = _mm_cmpeq_epi8(_mm_and_si128(d0, v_fe), v_e8);
        int mask0 = _mm_movemask_epi8(m0);
        if (mask0 != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask0);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask0);
#endif
            return candidate + (size_t)bit_idx;
        }
        candidate += 16;
    }
#endif
    while (candidate < limit && (data[candidate] & 0xFEU) != 0xE8U) ++candidate;
    return candidate;
}

bool xx_7zip_branch_properties_supported(size_t properties_size)
{
    return properties_size == 0U || properties_size == 4U;
}

static uint32_t xx_7zip_branch_start_ip(const uint8_t *properties, size_t properties_size)
{
    return properties_size == 4U ? xx_data_get_le32(properties) : 0U;
}

bool xx_7zip_branch_method_supported(uint64_t method, size_t properties_size)
{
    if (!xx_7zip_branch_properties_supported(properties_size)) return false;
    switch (method) {
        case XX_7ZIP_METHOD_BCJ:
        case XX_7ZIP_METHOD_PPC:
        case XX_7ZIP_METHOD_IA64:
        case XX_7ZIP_METHOD_ARM:
        case XX_7ZIP_METHOD_ARMT:
        case XX_7ZIP_METHOD_SPARC:
        case XX_7ZIP_METHOD_ARM64:
        case XX_7ZIP_METHOD_RISCV: return true;
        default: return false;
    }
}

static bool xx_7zip_x86_ms_byte(uint8_t value)
{
    return ((uint8_t)(value + 1U) & 0xFEU) == 0U;
}

static size_t xx_7zip_filter_bcj(uint8_t *data, size_t size, uint32_t ip, uint32_t *saved_mask)
{
    size_t pos = 0U;
    size_t limit;
    uint32_t mask = *saved_mask;
    if (!data || size < 5U) return 0U;
    limit = size - 4U;
    ip += 5U;
    while (pos < limit) {
        size_t candidate = pos;
        size_t distance;
        uint32_t value;
        uint32_t current;
        candidate = xx_bcj_scan_candidates(data, candidate, limit);
        distance = candidate - pos;
        pos = candidate;
        if (candidate >= limit) {
            *saved_mask = distance > 2U ? 0U : mask >> (unsigned)distance;
            return pos;
        }
        if (distance > 2U) {
            mask = 0U;
        } else {
            mask >>= (unsigned)distance;
            if (mask != 0U && (mask > 4U || mask == 3U || xx_7zip_x86_ms_byte(data[candidate + (mask >> 1U) + 1U]))) {
                mask = (mask >> 1U) | 4U;
                ++pos;
                continue;
            }
        }
        if (!xx_7zip_x86_ms_byte(data[candidate + 4U])) {
            mask = (mask >> 1U) | 4U;
            ++pos;
            continue;
        }
        value = xx_data_get_le32(data + candidate + 1U);
        current = ip + (uint32_t)candidate;
        value -= current;
        if (mask != 0U) {
            unsigned shift = (mask & 6U) << 2U;
            if (xx_7zip_x86_ms_byte((uint8_t)(value >> shift))) {
                value ^= ((UINT32_C(0x100) << shift) - 1U);
                value -= current;
            }
            mask = 0U;
        }
        data[candidate + 1U] = (uint8_t)value;
        data[candidate + 2U] = (uint8_t)(value >> 8U);
        data[candidate + 3U] = (uint8_t)(value >> 16U);
        data[candidate + 4U] = (uint8_t)(0U - ((value >> 24U) & 1U));
        pos = candidate + 5U;
    }
    *saved_mask = mask;
    return pos;
}

static void xx_7zip_filter_arm(uint8_t *data, size_t size, uint32_t ip)
{
    size_t pos = 0U;
    if (!data) return;
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    const __m128i v_eb = _mm_set1_epi8((char)-21); /* 0xEB */
    while (pos + 16U <= size) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(const void *)(data + pos));
        int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(chunk, v_eb));
        if ((mask & 0x8888) == 0) {
            pos += 16U;
            continue;
        }
        for (size_t step = 0; step < 16U; step += 4U) {
            if (data[pos + step + 3U] == 0xEBU) {
                uint32_t value = xx_data_get_le32(data + pos + step);
                value = (value << 2U) - (ip + (uint32_t)(pos + step) + 8U);
                value = (value >> 2U) & UINT32_C(0x00FFFFFF);
                xx_data_set_le32(data + pos + step, value | UINT32_C(0xEB000000));
            }
        }
        pos += 16U;
    }
#endif
    for (; pos + 4U <= size; pos += 4U) {
        uint32_t value;
        if (data[pos + 3U] != 0xEBU) continue;
        value = xx_data_get_le32(data + pos);
        value = (value << 2U) - (ip + (uint32_t)pos + 8U);
        value = (value >> 2U) & UINT32_C(0x00FFFFFF);
        xx_data_set_le32(data + pos, value | UINT32_C(0xEB000000));
    }
}

static size_t xx_7zip_filter_armt(uint8_t *data, size_t size, uint32_t ip)
{
    size_t pos;
    if (!data) return 0U;
    for (pos = 0U; pos + 4U <= size; pos += 2U) {
        uint32_t high = (uint32_t)(data[pos + 1U] ^ 8U);
        uint32_t low = data[pos + 3U];
        uint32_t value;
        if ((high & low) < UINT32_C(0xF8)) continue;
        value = (high << 19U) | ((uint32_t)(low & 7U) << 8U) | ((uint32_t)data[pos] << 11U) | data[pos + 2U];
        value -= (ip + (uint32_t)pos + 4U) >> 1U;
        data[pos] = (uint8_t)(value >> 11U);
        data[pos + 1U] = (uint8_t)(0xF0U | ((value >> 19U) & 7U));
        data[pos + 2U] = (uint8_t)value;
        data[pos + 3U] = (uint8_t)(0xF8U | (value >> 8U));
        pos += 2U;
    }
    return pos;
}

static void xx_7zip_filter_ppc(uint8_t *data, size_t size, uint32_t ip)
{
    size_t pos;
    if (!data) return;
    for (pos = 0U; pos + 4U <= size; pos += 4U) {
        uint32_t value;
        if ((data[pos] & 0xFCU) != 0x48U || (data[pos + 3U] & 3U) != 1U) continue;
        value = xx_data_get_be32(data + pos);
        value = (value - (ip + (uint32_t)pos)) & UINT32_C(0x03FFFFFF);
        xx_data_set_be32(data + pos, value | UINT32_C(0x48000000));
    }
}

static void xx_7zip_filter_sparc(uint8_t *data, size_t size, uint32_t ip)
{
    size_t pos;
    if (!data) return;
    for (pos = 0U; pos + 4U <= size; pos += 4U) {
        uint32_t value;
        if (!((data[pos] == 0x40U && (data[pos + 1U] & 0xC0U) == 0U) || (data[pos] == 0x7FU && data[pos + 1U] >= 0xC0U))) continue;
        value = xx_data_get_be32(data + pos);
        value = (value << 2U) - (ip + (uint32_t)pos);
        value = ((value & UINT32_C(0x01FFFFFF)) - UINT32_C(0x01000000)) ^ UINT32_C(0xFF000000);
        value = (value >> 2U) | UINT32_C(0x40000000);
        xx_data_set_be32(data + pos, value);
    }
}

static void xx_7zip_filter_ia64(uint8_t *data, size_t size, uint32_t ip)
{
    size_t bundle;
    if (!data || size < 16U) return;
    for (bundle = 0U; bundle + 16U <= size; bundle += 16U) {
        unsigned slot = (UINT32_C(0x334B0000) >> (data[bundle] & 0x1EU)) & 3U;
        if (slot == 0U) continue;
        ++slot;
        do {
            uint8_t *p;
            uint32_t raw;
            uint32_t value;
            p = data + bundle + (size_t)slot * 5U - 8U;
            if (((p[3] >> slot) & 15U) != 5U || ((((uint32_t)p[-1] | ((uint32_t)p[0] << 8U)) >> slot) & 0x70U) != 0U) continue;
            raw = xx_data_get_le32(p);
            value = raw >> slot;
            value = (value & UINT32_C(0xFFFFF)) | ((value & UINT32_C(0x800000)) >> 3U);
            value = (value << 4U) - (ip + (uint32_t)bundle);
            value = (value >> 4U) & UINT32_C(0x1FFFFF);
            value = (value + UINT32_C(0x700000)) & UINT32_C(0x8FFFFF);
            raw &= ~(UINT32_C(0x8FFFFF) << slot);
            raw |= value << slot;
            xx_data_set_le32(p, raw);
        } while (++slot <= 4U);
    }
}

static void xx_7zip_filter_arm64(uint8_t *data, size_t size, uint32_t ip)
{
    size_t pos;
    const uint32_t flag = UINT32_C(1) << 20U;
    const uint32_t adrp_mask = (UINT32_C(1) << 24U) - (flag << 1U);
    if (!data) return;
    for (pos = 0U; pos + 4U <= size; pos += 4U) {
        uint32_t value = xx_data_get_le32(data + pos);
        uint32_t pc = ip + (uint32_t)pos;
        if (((value - UINT32_C(0x94000000)) & UINT32_C(0xFC000000)) == 0U) {
            value = (value - (pc >> 2U)) & UINT32_C(0x03FFFFFF);
            xx_data_set_le32(data + pos, UINT32_C(0x94000000) | value);
        } else {
            uint32_t page_value = value - UINT32_C(0x90000000);
            uint32_t target;
            if ((page_value & UINT32_C(0x9F000000)) != 0U) continue;
            page_value += flag;
            if ((page_value & adrp_mask) != 0U) continue;
            target = (page_value & UINT32_C(0xFFFFFFE0)) | (page_value >> 26U);
            target -= (pc >> 9U) & ~UINT32_C(7);
            value &= UINT32_C(0x1F);
            value |= UINT32_C(0x90000000);
            value |= target << 26U;
            value |= UINT32_C(0x00FFFFE0) & ((target & ((flag << 1U) - 1U)) - flag);
            xx_data_set_le32(data + pos, value);
        }
    }
}

/* RISC-V's converter has three call forms: JAL and two AUIPC-based pairs.
 * Its wire representation permutes JAL target bytes and escapes AUIPC pairs.
 * Format and scan rules follow Igor Pavlov's public-domain Bra.c in 7-Zip
 * 26.03: https://github.com/ip7z/7zip/blob/26.03/C/Bra.c. */

static bool xx_7zip_riscv_pair_a(uint32_t first, uint32_t tag)
{
    return (((tag - 3U) ^ ((uint32_t)first << 8U)) & UINT32_C(0x000F8003)) == 0U;
}

static bool xx_7zip_riscv_pair_b(uint32_t first, uint32_t register_bits)
{
    uint32_t normalized = first - UINT32_C(0x00003108);
    return (normalized << 18U) < (register_bits & UINT32_C(0x1D));
}

static uint32_t xx_7zip_riscv_jal_with_immediate(uint32_t instruction, uint32_t immediate)
{
    return (instruction & UINT32_C(0x00000FFF)) | ((immediate << 11U) & UINT32_C(0x80000000)) | ((immediate << 20U) & UINT32_C(0x7FE00000)) |
           ((immediate << 9U) & UINT32_C(0x00100000)) | (immediate & UINT32_C(0x000FF000));
}

static size_t xx_7zip_filter_riscv(uint8_t *data, size_t size, uint32_t ip)
{
    size_t pos = 0U, limit;
    size &= ~(size_t)1U;
    if (!data || size <= 6U) return 0U;
    limit = size - 6U;
    while (pos < limit) {
        uint32_t first = ((uint32_t)xx_data_get_u16(data + pos, 2, 0, false) ^ 0x10U) + 1U;
        uint32_t instruction;
        uint32_t pc;
        if ((first & 0x77U) != 0U) {
            pos += 2U;
            continue;
        }
        pc = ip + (uint32_t)pos;
        if ((first & 8U) == 0U) {
            uint32_t saved, absolute, relative;
            first -= UINT32_C(0x81);
            if ((first & UINT32_C(0xD80)) != 0U) {
                pos += 2U;
                continue;
            }
            saved = (first + UINT32_C(0x70)) & UINT32_C(0xFFF);
            absolute = ((uint32_t)data[pos + 3U] << 1U) | ((uint32_t)data[pos + 2U] << 9U) | ((first & UINT32_C(0xF000)) << 5U);
            relative = absolute - pc;
            xx_data_set_u32(data + pos, 4, 0, xx_7zip_riscv_jal_with_immediate(saved, relative), false);
            pos += 4U;
            continue;
        }
        instruction = xx_data_get_u32(data + pos, 4, 0, false);
        if ((first & UINT32_C(0xE80)) == 0U) {
            uint32_t register_bits = instruction >> 27U;
            if (xx_7zip_riscv_pair_b(first, register_bits)) {
                uint32_t absolute = xx_data_get_u32(data + pos + 4U, 4, 0, true);
                uint32_t second = instruction >> 12U;
                uint32_t first_out;
                absolute -= pc;
                first_out = (register_bits << 7U) | UINT32_C(0x17);
                first_out += (absolute + UINT32_C(0x800)) & UINT32_C(0xFFFFF000);
                second |= absolute << 20U;
                xx_data_set_u32(data + pos, 4, 0, first_out, false);
                xx_data_set_u32(data + pos + 4U, 4, 0, second, false);
                pos += 8U;
            } else pos += 4U;
        } else {
            uint32_t second = xx_data_get_u32(data + pos + 4U, 4, 0, false);
            if (xx_7zip_riscv_pair_a(first, second)) {
                uint32_t first_out = (instruction & UINT32_C(0xFFFFF000)) | (second >> 20U);
                uint32_t second_out = (second << 12U) | UINT32_C(0x117);
                xx_data_set_u32(data + pos, 4, 0, second_out, false);
                xx_data_set_u32(data + pos + 4U, 4, 0, first_out, false);
                pos += 8U;
            } else pos += 6U;
        }
    }
    return pos;
}

bool xx_7zip_branch_decode(uint64_t method, const uint8_t *properties, size_t properties_size, const uint8_t *input, size_t input_size, uint8_t *output,
                           size_t output_size)
{
    uint32_t ip;
    uint32_t x86_mask = 0U;
    if ((!properties && properties_size) || (!input && input_size) || (!output && output_size) || input_size != output_size ||
        !xx_7zip_branch_method_supported(method, properties_size))
        return false;
    if (output_size) xx_mem_copy(output, input, output_size);
    ip = xx_7zip_branch_start_ip(properties, properties_size);
    switch (method) {
        case XX_7ZIP_METHOD_BCJ: (void)xx_7zip_filter_bcj(output, output_size, ip, &x86_mask); break;
        case XX_7ZIP_METHOD_PPC: xx_7zip_filter_ppc(output, output_size, ip); break;
        case XX_7ZIP_METHOD_IA64: xx_7zip_filter_ia64(output, output_size, ip); break;
        case XX_7ZIP_METHOD_ARM: xx_7zip_filter_arm(output, output_size, ip); break;
        case XX_7ZIP_METHOD_ARMT: xx_7zip_filter_armt(output, output_size, ip); break;
        case XX_7ZIP_METHOD_SPARC: xx_7zip_filter_sparc(output, output_size, ip); break;
        case XX_7ZIP_METHOD_ARM64: xx_7zip_filter_arm64(output, output_size, ip); break;
        case XX_7ZIP_METHOD_RISCV: xx_7zip_filter_riscv(output, output_size, ip); break;
        default: return false;
    }
    return true;
}

bool xx_7zip_branch_state_init(xx_7zip_branch_state *state, uint64_t method, const uint8_t *properties, size_t properties_size)
{
    if (!state || (!properties && properties_size) || !xx_7zip_branch_method_supported(method, properties_size)) return false;
    state->method = method;
    state->ip = xx_7zip_branch_start_ip(properties, properties_size);
    state->x86_mask = 0U;
    return true;
}

size_t xx_7zip_branch_process(xx_7zip_branch_state *state, uint8_t *data, size_t size, bool final)
{
    size_t processed;
    if (!state || (!data && size)) return SIZE_MAX;
    switch (state->method) {
        case XX_7ZIP_METHOD_BCJ: processed = xx_7zip_filter_bcj(data, size, state->ip, &state->x86_mask); break;
        case XX_7ZIP_METHOD_ARMT: processed = xx_7zip_filter_armt(data, size, state->ip); break;
        case XX_7ZIP_METHOD_RISCV: processed = xx_7zip_filter_riscv(data, size, state->ip); break;
        case XX_7ZIP_METHOD_IA64:
            processed = size & ~(size_t)15U;
            xx_7zip_filter_ia64(data, processed, state->ip);
            break;
        case XX_7ZIP_METHOD_PPC:
        case XX_7ZIP_METHOD_ARM:
        case XX_7ZIP_METHOD_SPARC:
        case XX_7ZIP_METHOD_ARM64:
            processed = size & ~(size_t)3U;
            if (state->method == XX_7ZIP_METHOD_PPC) xx_7zip_filter_ppc(data, processed, state->ip);
            else if (state->method == XX_7ZIP_METHOD_ARM) xx_7zip_filter_arm(data, processed, state->ip);
            else if (state->method == XX_7ZIP_METHOD_SPARC) xx_7zip_filter_sparc(data, processed, state->ip);
            else xx_7zip_filter_arm64(data, processed, state->ip);
            break;
        default: return SIZE_MAX;
    }
    if (final) processed = size;
    state->ip += (uint32_t)processed;
    return processed;
}

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

static inline size_t xx_7zip_bcj2_scan_and_copy(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i = 0;
    const __m128i v_e8 = _mm_set1_epi8((char)-24); /* 0xE8 */
    const __m128i v_fe = _mm_set1_epi8((char)-2);  /* 0xFE */
    const __m128i v_0f = _mm_set1_epi8((char)0x0F);

    while (i + 64 <= len) {
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
        if (mask1 != 0) {
            _mm_storeu_si128((__m128i *)(void *)(dst + i + 16), d1);
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask1);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask1);
#endif
            return i + 16 + (size_t)bit_idx;
        }

        __m128i d2 = _mm_loadu_si128((const __m128i *)(const void *)(src + i + 32));
        __m128i m2 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d2, v_fe), v_e8), _mm_cmpeq_epi8(d2, v_0f));
        int mask2 = _mm_movemask_epi8(m2);
        _mm_storeu_si128((__m128i *)(void *)(dst + i + 16), d1);
        if (mask2 != 0) {
            _mm_storeu_si128((__m128i *)(void *)(dst + i + 32), d2);
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask2);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask2);
#endif
            return i + 32 + (size_t)bit_idx;
        }

        __m128i d3 = _mm_loadu_si128((const __m128i *)(const void *)(src + i + 48));
        __m128i m3 = _mm_or_si128(_mm_cmpeq_epi8(_mm_and_si128(d3, v_fe), v_e8), _mm_cmpeq_epi8(d3, v_0f));
        int mask3 = _mm_movemask_epi8(m3);
        _mm_storeu_si128((__m128i *)(void *)(dst + i + 32), d2);
        _mm_storeu_si128((__m128i *)(void *)(dst + i + 48), d3);
        if (mask3 != 0) {
            unsigned long bit_idx;
#if defined(_MSC_VER)
            _BitScanForward(&bit_idx, (unsigned long)mask3);
#else
            bit_idx = (unsigned long)__builtin_ctz((unsigned int)mask3);
#endif
            return i + 48 + (size_t)bit_idx;
        }
        i += 64;
    }
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
static inline size_t xx_7zip_bcj2_scan_and_copy(uint8_t *dst, const uint8_t *src, size_t len)
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

bool xx_7zip_bcj2_decode(const uint8_t *const inputs[4], const size_t input_sizes[4], const uint8_t *properties, size_t properties_size, uint8_t *output,
                         size_t output_size)
{
    const uint8_t *main_data, *call_data, *jump_data, *range_data;
    size_t main_size, call_size, jump_size, range_size;
    size_t main_pos = 0U, call_pos = 0U, jump_pos = 0U, range_pos = 0U;
    size_t output_pos = 0U;
    uint32_t range = UINT32_MAX, code = 0U, ip;
    uint16_t probabilities[258];
    uint8_t previous = 0U, first;

    if (!inputs || !input_sizes || (!properties && properties_size) || (!output && output_size) || (!inputs[0] && input_sizes[0]) || (!inputs[1] && input_sizes[1]) ||
        (!inputs[2] && input_sizes[2]) || (!inputs[3] && input_sizes[3]) || !xx_7zip_branch_properties_supported(properties_size) || input_sizes[3] < 5U ||
        (input_sizes[1] & 3U) != 0U || (input_sizes[2] & 3U) != 0U)
        return false;

    main_data = inputs[0]; main_size = input_sizes[0];
    call_data = inputs[1]; call_size = input_sizes[1];
    jump_data = inputs[2]; jump_size = input_sizes[2];
    range_data = inputs[3]; range_size = input_sizes[3];

    ip = xx_7zip_branch_start_ip(properties, properties_size);
    for (size_t i = 0U; i < 258U; ++i) probabilities[i] = 1024U;

    first = range_data[range_pos++];
    if (first != 0U) return false;
    for (size_t i = 0U; i < 4U; ++i) {
        code = (code << 8U) | range_data[range_pos++];
    }
    if (code == UINT32_MAX) return false;

    while (main_pos < main_size) {
        if (previous != 0x0FU) {
            size_t avail_in = main_size - main_pos;
            size_t avail_out = output_size - output_pos;
            size_t max_run = avail_in < avail_out ? avail_in : avail_out;

            if (max_run > 0) {
                size_t copied = xx_7zip_bcj2_scan_and_copy(output + output_pos, main_data + main_pos, max_run);
                if (copied) {
                    output_pos += copied;
                    main_pos += copied;
                    ip += (uint32_t)copied;
                    previous = main_data[main_pos - 1];
                    if (main_pos >= main_size || output_pos >= output_size) continue;
                }
            }
        }

        uint8_t opcode = main_data[main_pos++];
        bool branch = opcode == 0xE8U || opcode == 0xE9U || (previous == 0x0FU && (opcode & 0xF0U) == 0x80U);
        if (output_pos >= output_size) return false;
        output[output_pos++] = opcode;
        ++ip;

        if (branch) {
            uint16_t *probability = opcode == 0xE8U ? &probabilities[2U + previous] : &probabilities[opcode == 0xE9U ? 1U : 0U];
            uint32_t bound;
            if (range < (UINT32_C(1) << 24U)) {
                if (range_pos >= range_size) return false;
                range <<= 8U;
                code = (code << 8U) | range_data[range_pos++];
            }
            bound = (range >> 11U) * (uint32_t)(*probability);
            if (code < bound) {
                range = bound;
                *probability = (uint16_t)(*probability + ((2048U - *probability) >> 5U));
                previous = opcode;
            } else {
                range -= bound;
                code -= bound;
                *probability = (uint16_t)(*probability - (*probability >> 5U));

                const uint8_t *sel_ptr = (opcode == 0xE8U) ? (call_data + call_pos) : (jump_data + jump_pos);
                size_t *target_pos = (opcode == 0xE8U) ? &call_pos : &jump_pos;
                size_t target_size = (opcode == 0xE8U) ? call_size : jump_size;

                if (*target_pos > target_size || target_size - *target_pos < 4U || output_size - output_pos < 4U) return false;

#if defined(_M_IX86) || defined(_M_X64)
                uint32_t target = _byteswap_ulong(*(const uint32_t *)(const void *)sel_ptr);
#elif (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                uint32_t target = __builtin_bswap32(*(const uint32_t *)(const void *)sel_ptr);
#else
                uint32_t target = ((uint32_t)sel_ptr[0] << 24U) | ((uint32_t)sel_ptr[1] << 16U) |
                                  ((uint32_t)sel_ptr[2] << 8U)  | (uint32_t)sel_ptr[3];
#endif
                *target_pos += 4U;
                ip += 4U;
                target -= ip;

#if defined(_M_IX86) || defined(_M_X64) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
                *(uint32_t *)(void *)(output + output_pos) = target;
                output_pos += 4U;
                previous = (uint8_t)(target >> 24U);
#else
                output[output_pos++] = (uint8_t)target;
                output[output_pos++] = (uint8_t)(target >> 8U);
                output[output_pos++] = (uint8_t)(target >> 16U);
                output[output_pos++] = (uint8_t)(target >> 24U);
                previous = (uint8_t)(target >> 24U);
#endif
                continue;
            }
        } else {
            previous = opcode;
        }
    }
    return output_pos == output_size && call_pos == call_size && jump_pos == jump_size && code == 0U;
}

