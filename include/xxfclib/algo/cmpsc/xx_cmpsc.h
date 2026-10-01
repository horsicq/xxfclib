/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_CMPSC_H
#define XXFCLIB_ALGO_CMPSC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Decode the complete ZIP method-16 payload: its six-byte dictionary header,
 * expansion dictionary (raw or raw-Deflate compressed), and MSB-first CMPSC
 * index symbols. output_size is the exact decoded member size. The decoder
 * supports symbol widths 9 through 13, limits dictionary storage to 64 KiB,
 * validates predecessor chains and output ranges, and checks cancellation.
 * Unused bits in the last source byte are ignored, as defined by CMPSC.
 * written is reset to zero on failure. NULL output is valid only for size 0.
 */
XXFC_API bool xx_cmpsc_zip_decode_memory(const uint8_t *input,
                                        size_t input_size,
                                        uint8_t *output,
                                        size_t output_size,
                                        size_t *written,
                                        xx_pd_struct *pd);

/** Maximum payload size for the baseline CMPSC encoder, or zero on size
 * overflow. The result includes the header and a full 512-entry dictionary;
 * even an empty input has a nonzero valid bound. */
XXFC_API size_t xx_cmpsc_zip_encode_bound(size_t input_size);

/** Build a static 9-bit LZ78 phrase dictionary and emit a complete ZIP method-16
 * payload. The dictionary is raw-Deflate compressed when that saves space.
 * Phrases contain at most 256 bytes. This portable baseline does not require
 * mainframe hardware. NULL input is allowed only for an empty input; input and
 * output must not overlap. written is reset to zero on failure/cancellation.
 */
XXFC_API bool xx_cmpsc_zip_encode_memory(const uint8_t *input,
                                        size_t input_size,
                                        uint8_t *output,
                                        size_t output_capacity,
                                        size_t *written,
                                        xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
#endif
