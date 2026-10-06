/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_PDF_DECODE_H
#define XXFCLIB_PDF_DECODE_H

#include "xxfclib/data/xx_pd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_pdf_filter {
    XX_PDF_FILTER_FLATE,
    XX_PDF_FILTER_LZW,
    XX_PDF_FILTER_ASCII85,
    XX_PDF_FILTER_ASCIIHEX,
    XX_PDF_FILTER_RUNLENGTH,
    XX_PDF_FILTER_DCT,
    XX_PDF_FILTER_JPX,
    XX_PDF_FILTER_UNSUPPORTED
} xx_pdf_filter;

/* Populate PDF defaults explicitly: 1, 1, 8, 1, 1. EarlyChange zero is a
 * valid explicit value; it is therefore never replaced by a default here. */
typedef struct xx_pdf_decode_params {
    uint32_t predictor, colors, bits_per_component, columns, early_change;
} xx_pdf_decode_params;

typedef struct xx_pdf_filter_spec {
    xx_pdf_filter filter;
    xx_pdf_decode_params params;
} xx_pdf_filter_spec;

/* Internal native decoder. Inputs are borrowed. Output is xx_mem_alloc-owned
 * and NULL/zero on failure (also on successful empty output). output_limit
 * bounds the final result after its predictor; intermediate filter payloads
 * are bounded by memory_limit. memory_limit bounds live owned payload
 * capacities and allocated decoder workspace, including the preceding stage.
 * SIZE_MAX means unlimited. Flate includes the library's configured I/O
 * buffer and 32 KiB history in its workspace budget. DCT/JPX preserve
 * their encoded payload and may occur only as the final filter. */
bool xx_pdf_decode_stream(const uint8_t *input, size_t input_size,
    const xx_pdf_filter_spec *filters, size_t filter_count, size_t output_limit,
    size_t memory_limit, xx_pd_struct *pd, uint8_t **output,
    size_t *output_size);

/* Same decoder, also reporting the actual retained allocation capacity.
 * The capacity includes padding reserved during growth or before PNG row
 * marker removal, and is zero on failure or on an empty allocation. */
bool xx_pdf_decode_stream_ex(const uint8_t *input, size_t input_size,
    const xx_pdf_filter_spec *filters, size_t filter_count, size_t output_limit,
    size_t memory_limit, xx_pd_struct *pd, uint8_t **output,
    size_t *output_size, size_t *output_capacity);

#ifdef __cplusplus
}
#endif

#endif
