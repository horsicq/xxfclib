/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_LZMA_STREAM_INTERNAL_H
#define XXFCLIB_LZMA_STREAM_INTERNAL_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

/* Private codec interface: no archive metadata or format dependencies. */
#define XX_LZMA_STREAM_UNKNOWN_SIZE UINT64_MAX

typedef struct xx_lzma_stream_props_s {
    unsigned lc;
    unsigned lp;
    unsigned pb;
    uint32_t dictionary_size;
    uint64_t declared_size;
} xx_lzma_stream_props;

typedef enum xx_lzma_stream_result_e {
    XX_LZMA_STREAM_FAILED = 0,
    XX_LZMA_STREAM_FINISHED = 1,
    XX_LZMA_STREAM_PARTIAL = 2
} xx_lzma_stream_result;

typedef struct xx_lzma_stream_info_s {
    uint64_t produced;
    uint64_t consumed; /* Raw data bytes, excluding any container header. */
    bool end_marker;
    bool zero_data;
    bool allocation_failed;
} xx_lzma_stream_info;

typedef struct xx_lzma_stream_decoder_s xx_lzma_stream_decoder;

bool xx_lzma_stream_read_exact_at(xx_io_device *device, int64_t offset, void *data, size_t size, xx_pd_struct *pd);
size_t xx_lzma_stream_model_entries(const xx_lzma_stream_props *properties);

/* Devices and progress data are borrowed. A NULL destination measures only.
 * The decoder may be reused for consecutive independent streams. */
xx_lzma_stream_decoder *xx_lzma_stream_decoder_create(xx_io_device *device, xx_io_device *destination, xx_pd_struct *pd);
void xx_lzma_stream_decoder_free(xx_lzma_stream_decoder *decoder);

/* Decode the raw extent [data_offset, data_end). Known-size streams may omit
 * the end marker; successful completion verifies the final range-coder state.
 * A nonzero probe_output permits a PARTIAL result before completion. Pending
 * output is not flushed on PARTIAL. Results always report produced/consumed
 * counts and distinguish allocation failures from invalid compressed data. */
xx_lzma_stream_result xx_lzma_stream_decode(xx_lzma_stream_decoder *decoder, const xx_lzma_stream_props *properties, int64_t data_offset, int64_t data_end,
                                            uint64_t probe_output, xx_lzma_stream_info *info);

#endif /* XXFCLIB_LZMA_STREAM_INTERNAL_H */
