/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_wiilz77.h @brief Nintendo LZ10 and LZ11 decoder. */

#ifndef XXFCLIB_ALGO_WIILZ77_H
#define XXFCLIB_ALGO_WIILZ77_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_wiilz77_variant_e {
    XX_WIILZ77_VARIANT_UNKNOWN = 0,
    XX_WIILZ77_VARIANT_LZ10 = 0x10,
    XX_WIILZ77_VARIANT_LZ11 = 0x11
} xx_wiilz77_variant_t;

typedef struct xx_wiilz77_header_s {
    xx_wiilz77_variant_t variant;
    uint64_t uncompressed_size;
    size_t header_size;
    bool has_tag;
} xx_wiilz77_header;

/** Parse a bare or ``LZ77``-tagged Nintendo LZ10/LZ11 header. */
XXFC_API bool xx_wiilz77_parse_header(const uint8_t *input,
                                      size_t input_size,
                                      xx_wiilz77_header *header);

/** Decode one complete bare or tagged LZ10/LZ11 member.  The output buffer
 * must have exactly the declared size.  On success @p consumed_size receives
 * the number of bytes used from the member, excluding container padding. */
XXFC_API bool xx_wiilz77_decompress_memory(const uint8_t *input,
                                           size_t input_size,
                                           uint8_t *output,
                                           size_t output_size,
                                           size_t *consumed_size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_ALGO_WIILZ77_H */
