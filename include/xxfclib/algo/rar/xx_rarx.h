/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_rarx.h
 * @brief Stateful decompressor for the compression streams used by RAR archives.
 *
 * The RAR container reader supplies the unpack generation, bounded packed
 * extent, expected output size, dictionary size, and solid-continuation flag.
 * Encryption and multi-volume assembly are container-layer responsibilities.
 */

#ifndef XX_RARX_H
#define XX_RARX_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_RARX_DEFAULT_MAX_WINDOW (256u * 1024u * 1024u)

typedef enum xx_rarx_method_e {
    XX_RARX_METHOD_15 = 15,
    XX_RARX_METHOD_20 = 20,
    XX_RARX_METHOD_29 = 29,
    XX_RARX_METHOD_50 = 50,
    XX_RARX_METHOD_70 = 70
} xx_rarx_method_t;

typedef enum xx_rarx_status_e {
    XX_RARX_STATUS_OK = 0,
    XX_RARX_STATUS_INVALID_ARGUMENT,
    XX_RARX_STATUS_NO_MEMORY,
    XX_RARX_STATUS_IO_ERROR,
    XX_RARX_STATUS_TRUNCATED,
    XX_RARX_STATUS_CORRUPT,
    XX_RARX_STATUS_UNSUPPORTED_VERSION,
    XX_RARX_STATUS_UNSUPPORTED_FILTER,
    XX_RARX_STATUS_LIMIT,
    XX_RARX_STATUS_CANCELLED,
    XX_RARX_STATUS_POISONED
} xx_rarx_status_t;

typedef struct xx_rarx_result_s {
    int64_t input_consumed;
    int64_t output_written;
    xx_rarx_status_t status;
} xx_rarx_result;

typedef struct xx_rarx_decoder xx_rarx_decoder;

/**
 * Create a reusable decoder. max_window_size==0 selects the safe default.
 * The limit caps both LZ dictionaries and RAR3 PPMd model allocations.
 */
XXFC_API xx_rarx_decoder *xx_rarx_decoder_create(uint64_t max_window_size);

/** Drop all solid-stream history and clear a poisoned session. */
XXFC_API void xx_rarx_decoder_reset(xx_rarx_decoder *decoder);

XXFC_API void xx_rarx_decoder_free(xx_rarx_decoder *decoder);

XXFC_API xx_rarx_status_t xx_rarx_decoder_last_status(
    const xx_rarx_decoder *decoder);

/**
 * Decode one compressed RAR member from a strictly bounded device range.
 * expected_size is mandatory. Set solid_continuation only when the member is
 * the next member of the same solid chain and all preceding members succeeded.
 */
XXFC_API bool xx_rarx_decoder_unpack_device(
    xx_rarx_decoder *decoder, xx_io_device *source, int64_t source_offset,
    int64_t compressed_size, xx_io_device *destination, int64_t expected_size,
    xx_rarx_method_t method, uint64_t window_size, bool solid_continuation,
    xx_rarx_result *result, xx_pd_struct *progress);

/** Decode one member held in memory into a caller-owned exact-size buffer. */
XXFC_API bool xx_rarx_decoder_unpack_memory(
    xx_rarx_decoder *decoder, const void *source, size_t compressed_size,
    void *destination, size_t expected_size, xx_rarx_method_t method,
    uint64_t window_size, bool solid_continuation, xx_rarx_result *result,
    xx_pd_struct *progress);

/** One-shot helper for a non-solid member. */
XXFC_API bool xx_rarx_unpack_device(
    xx_io_device *source, int64_t source_offset, int64_t compressed_size,
    xx_io_device *destination, int64_t expected_size, xx_rarx_method_t method,
    uint64_t window_size, xx_rarx_result *result, xx_pd_struct *progress);

#ifdef __cplusplus
}
#endif

#endif /* XX_RARX_H */
