/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_data_platform.h"
#include "xx_data_filter.h"

#ifdef XX_DATA_ARM_NEON

#include "xxfclib/data/xx_pd.h"
#include "xxfclib/memory/xx_memory.h"
#include <arm_neon.h>

#if defined(_MSC_VER)
#define XX_DATA_NEON_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define XX_DATA_NEON_INLINE inline __attribute__((always_inline))
#else
#define XX_DATA_NEON_INLINE inline
#endif

static XX_DATA_NEON_INLINE int xx_data_neon_any(uint8x16_t value)
{
    uint64x2_t words = vreinterpretq_u64_u8(value);
    return (vgetq_lane_u64(words, 0) | vgetq_lane_u64(words, 1)) != 0;
}

static XX_DATA_NEON_INLINE int xx_data_neon_should_stop(xx_pd_struct *pd, size_t at, size_t start)
{
    /* SIMD advances in multiples of 16 bytes, so this polls at most every
     * 16 KiB regardless of the caller's absolute start alignment. */
    return pd && (((at - start) & 0x3fffU) == 0) && xx_pd_is_stopped(pd);
}

int64_t xx_data_find_bytes_neon(const uint8_t *pdata, size_t data_size, size_t start_offset, const uint8_t *pat, size_t pattern_size, xx_pd_struct *pd)
{
    size_t i;

    if (!pdata || !pat || pattern_size == 0 || start_offset + pattern_size > data_size || start_offset + pattern_size < start_offset) return -1;
    if (xx_pd_is_stopped(pd)) return -1;

    i = start_offset;
    if (pattern_size == 1) {
        uint8x16_t wanted = vdupq_n_u8(pat[0]);

        while (data_size - i >= 16) {
            uint8x16_t matches;
            if (xx_data_neon_should_stop(pd, i, start_offset)) return -1;
            matches = vceqq_u8(vld1q_u8(pdata + i), wanted);
            if (xx_data_neon_any(matches)) {
                uint8_t lanes[16];
                size_t lane;
                vst1q_u8(lanes, matches);
                for (lane = 0; lane < 16; ++lane) {
                    if (lanes[lane] != 0) return (int64_t)(i + lane);
                }
            }
            i += 16;
        }
        for (; i < data_size; ++i) {
            if (pdata[i] == pat[0]) return (int64_t)i;
        }
        return -1;
    }

    {
        size_t limit = data_size - pattern_size;
        size_t idx1;
        size_t idx2;
        uint8x16_t wanted1;
        uint8x16_t wanted2;

        xx_select_filter_indices(pat, pattern_size, &idx1, &idx2);
        wanted1 = vdupq_n_u8(pat[idx1]);
        wanted2 = vdupq_n_u8(pat[idx2]);

        /* Four independent vectors hide load/compare latency and amortize
         * loop control and the reduction across 64 candidate starts.  The
         * combined test keeps the common no-candidate path free of stores. */
        while (i <= limit && limit - i >= 63) {
            uint8x16_t matches0;
            uint8x16_t matches1;
            uint8x16_t matches2;
            uint8x16_t matches3;
            uint8x16_t any;

            if (xx_data_neon_should_stop(pd, i, start_offset)) return -1;
            matches0 = vandq_u8(vceqq_u8(vld1q_u8(pdata + i + idx1), wanted1), vceqq_u8(vld1q_u8(pdata + i + idx2), wanted2));
            matches1 = vandq_u8(vceqq_u8(vld1q_u8(pdata + i + 16 + idx1), wanted1), vceqq_u8(vld1q_u8(pdata + i + 16 + idx2), wanted2));
            matches2 = vandq_u8(vceqq_u8(vld1q_u8(pdata + i + 32 + idx1), wanted1), vceqq_u8(vld1q_u8(pdata + i + 32 + idx2), wanted2));
            matches3 = vandq_u8(vceqq_u8(vld1q_u8(pdata + i + 48 + idx1), wanted1), vceqq_u8(vld1q_u8(pdata + i + 48 + idx2), wanted2));
            any = vorrq_u8(vorrq_u8(matches0, matches1), vorrq_u8(matches2, matches3));
            if (xx_data_neon_any(any)) {
                uint8_t lanes[64];
                size_t lane;

                vst1q_u8(lanes, matches0);
                vst1q_u8(lanes + 16, matches1);
                vst1q_u8(lanes + 32, matches2);
                vst1q_u8(lanes + 48, matches3);
                for (lane = 0; lane < 64; ++lane) {
                    size_t match_offset;
                    if (lanes[lane] == 0) continue;
                    match_offset = i + lane;
                    if (pattern_size <= 2 || xx_mem_compare(pdata + match_offset, pat, pattern_size) == 0) {
                        return (int64_t)match_offset;
                    }
                }
            }
            i += 64;
        }

        /* Each iteration tests 16 candidate starts. The subtraction form
         * keeps both selected-byte loads in bounds without overflow. */
        while (i <= limit && limit - i >= 15) {
            uint8x16_t matches;
            if (xx_data_neon_should_stop(pd, i, start_offset)) return -1;
            matches = vandq_u8(vceqq_u8(vld1q_u8(pdata + i + idx1), wanted1), vceqq_u8(vld1q_u8(pdata + i + idx2), wanted2));
            if (xx_data_neon_any(matches)) {
                uint8_t lanes[16];
                size_t lane;
                vst1q_u8(lanes, matches);
                for (lane = 0; lane < 16; ++lane) {
                    size_t match_offset;
                    if (lanes[lane] == 0) continue;
                    match_offset = i + lane;
                    if (pattern_size <= 2 || xx_mem_compare(pdata + match_offset, pat, pattern_size) == 0) {
                        return (int64_t)match_offset;
                    }
                }
            }
            i += 16;
        }

        for (; i <= limit; ++i) {
            if (pdata[i + idx1] == pat[idx1] && pdata[i + idx2] == pat[idx2] && (pattern_size <= 2 || xx_mem_compare(pdata + i, pat, pattern_size) == 0)) {
                return (int64_t)i;
            }
        }
    }
    return -1;
}

#undef XX_DATA_NEON_INLINE

static int xx_data_masked_equal_neon(const uint8_t *data, const uint8_t *value, const uint8_t *mask, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i) {
        if ((uint8_t)(data[i] & mask[i]) != value[i]) return 0;
    }
    return 1;
}

int64_t xx_data_find_masked_neon(const uint8_t *data, size_t size, const uint8_t *value, const uint8_t *mask, size_t pattern_size, size_t idx1, size_t idx2)
{
    size_t last;
    size_t p = 0;
    uint8x16_t wanted1;
    uint8x16_t wanted2;

    if (!data || !value || !mask || pattern_size == 0 || pattern_size > size || idx1 >= pattern_size || idx2 >= pattern_size) return -1;

    last = size - pattern_size;
    wanted1 = vdupq_n_u8(value[idx1]);
    wanted2 = vdupq_n_u8(value[idx2]);
    while (p <= last && last - p >= 15) {
        uint8x16_t matches = vandq_u8(vceqq_u8(vld1q_u8(data + p + idx1), wanted1), vceqq_u8(vld1q_u8(data + p + idx2), wanted2));
        if (xx_data_neon_any(matches)) {
            uint8_t lanes[16];
            size_t lane;
            vst1q_u8(lanes, matches);
            for (lane = 0; lane < 16; ++lane) {
                if (lanes[lane] != 0 && xx_data_masked_equal_neon(data + p + lane, value, mask, pattern_size)) {
                    return (int64_t)(p + lane);
                }
            }
        }
        p += 16;
    }

    for (; p <= last; ++p) {
        if (data[p + idx1] == value[idx1] && data[p + idx2] == value[idx2] && xx_data_masked_equal_neon(data + p, value, mask, pattern_size)) {
            return (int64_t)p;
        }
    }
    return -1;
}

#endif
