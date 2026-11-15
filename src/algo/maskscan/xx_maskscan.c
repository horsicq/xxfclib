/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_maskscan.c
 * @brief Portable reference search and run-time dispatch for masked byte probes.
 */

#include "platforms/xx_maskscan_platform.h"
#include "xxfclib/global/xx_global.h"

size_t xx_maskscan_find_scalar(const uint8_t *data, size_t first, size_t last,
                               const xx_maskscan_probe *probes, size_t count)
{
    const uint8_t *lead = data + probes[0].offset;
    uint8_t lead_mask = probes[0].mask, lead_value = probes[0].value;
    size_t position = first;
    for (;;) {
        if ((lead[position] & lead_mask) == lead_value) {
            size_t k = 1;
            while (k < count && (data[position + probes[k].offset] & probes[k].mask) == probes[k].value) ++k;
            if (k == count) return position;
        }
        if (position == last) return SIZE_MAX;
        ++position;
    }
}

static bool xx_maskscan_valid(const void *data, size_t size, size_t first, size_t last,
                              const xx_maskscan_probe *probes, size_t count)
{
    size_t k;
    if (!data || !probes || count == 0 || count > XX_MASKSCAN_MAX_PROBES || first > last || last >= size) return false;
    for (k = 0; k < count; ++k) {
        if (probes[k].offset >= size - last) return false;
        if ((probes[k].value & (uint8_t)~probes[k].mask) != 0) return false;
    }
    return true;
}

/* ------------------------------------------------------------- dispatch */

bool xx_maskscan_backend_available(int backend)
{
    if (backend == XX_MASKSCAN_BACKEND_SCALAR) return true;
#ifdef XX_MASKSCAN_X86
    if (backend == XX_MASKSCAN_BACKEND_SSE2) return xx_is_sse2_enabled();
    if (backend == XX_MASKSCAN_BACKEND_AVX2) return xx_is_avx2_enabled();
#endif
    return false;
}

static size_t xx_maskscan_run(int backend, const uint8_t *data, size_t first, size_t last,
                              const xx_maskscan_probe *probes, size_t count)
{
#ifdef XX_MASKSCAN_X86
    if (backend == XX_MASKSCAN_BACKEND_AVX2) return xx_maskscan_find_avx2(data, first, last, probes, count);
    if (backend == XX_MASKSCAN_BACKEND_SSE2) return xx_maskscan_find_sse2(data, first, last, probes, count);
#else
    (void)backend;
#endif
    return xx_maskscan_find_scalar(data, first, last, probes, count);
}

size_t xx_maskscan_find(const void *data, size_t size, size_t first, size_t last,
                        const xx_maskscan_probe *probes, size_t count)
{
    int backend = XX_MASKSCAN_BACKEND_SCALAR;
    if (!xx_maskscan_valid(data, size, first, last, probes, count)) return SIZE_MAX;
#ifdef XX_MASKSCAN_X86
    if (xx_is_avx2_enabled()) backend = XX_MASKSCAN_BACKEND_AVX2;
    else if (xx_is_sse2_enabled()) backend = XX_MASKSCAN_BACKEND_SSE2;
#endif
    return xx_maskscan_run(backend, (const uint8_t *)data, first, last, probes, count);
}

bool xx_maskscan_find_backend(int backend, const void *data, size_t size, size_t first,
                              size_t last, const xx_maskscan_probe *probes, size_t count,
                              size_t *found)
{
    if (!found || !xx_maskscan_backend_available(backend)) return false;
    *found = xx_maskscan_valid(data, size, first, last, probes, count) ?
             xx_maskscan_run(backend, (const uint8_t *)data, first, last, probes, count) : SIZE_MAX;
    return true;
}
