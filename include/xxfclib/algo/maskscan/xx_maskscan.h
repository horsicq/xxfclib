/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_maskscan.h
 * @brief Find positions where several masked bytes hold at fixed distances.
 *
 * A probe states that the byte at a fixed distance from a candidate position
 * equals a value in the bits selected by a mask. The search returns the
 * lowest position at which every probe holds. It is a prefilter for byte
 * patterns with wildcards, nibble masks and gaps, where no literal run is
 * long enough for a substring search. SSE2/AVX2 backends test 16/32
 * positions at once and are chosen at run time; the portable scalar code is
 * the reference they must equal.
 */

#ifndef XX_MASKSCAN_H
#define XX_MASKSCAN_H

#include "xxfclib/xxfc_defs.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Backend selectors for xx_maskscan_find_backend. */
#define XX_MASKSCAN_BACKEND_SCALAR 0
#define XX_MASKSCAN_BACKEND_SSE2 1
#define XX_MASKSCAN_BACKEND_AVX2 2

/** Most probes one search can test. */
#define XX_MASKSCAN_MAX_PROBES 4

/** (data[position + offset] & mask) == value must hold. */
typedef struct xx_maskscan_probe {
    size_t offset;
    uint8_t mask;
    uint8_t value; /**< bits outside mask must be zero */
} xx_maskscan_probe;

/** True when the backend is compiled in and enabled on this CPU. */
XXFC_API bool xx_maskscan_backend_available(int backend);

/** Lowest position p, first <= p <= last, at which every probe holds.
 * Every probed byte must lie in data: last + (largest offset) < size.
 * Returns SIZE_MAX when no position qualifies, when first > last, or when
 * the arguments are invalid (no probes, more than XX_MASKSCAN_MAX_PROBES,
 * a probe outside data, or a value bit outside its mask). */
XXFC_API size_t xx_maskscan_find(const void *data, size_t size, size_t first, size_t last,
                                 const xx_maskscan_probe *probes, size_t count);

/** xx_maskscan_find with an explicit backend, for differential tests.
 * Returns false and leaves *found untouched when the backend is not
 * available. */
XXFC_API bool xx_maskscan_find_backend(int backend, const void *data, size_t size, size_t first,
                                       size_t last, const xx_maskscan_probe *probes, size_t count,
                                       size_t *found);

#ifdef __cplusplus
}
#endif

#endif /* XX_MASKSCAN_H */
