/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_LZH_H
#define XXFCLIB_ALGO_LZH_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

XXFC_API bool xx_lzh1_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    size_t *written);

/** Decode exactly one bounded LH1 stream without retaining its payload.
 * Devices are borrowed and used at their current positions. Short transfers
 * are retried. Cancellation and progress are checked every 4 KiB. */
XXFC_API bool xx_lzh1_decode_to_device(xx_io_device *input, uint64_t input_size,
                                      xx_io_device *output, uint64_t output_size,
                                      uint64_t *written, xx_pd_struct *pd);

/**
 * @brief Decode an LHA -lh4-/-lh5-/-lh6-/-lh7- stream.
 *
 * These four share one algorithm and differ only in window size, which @p
 * method selects: 4 = 4 KiB, 5 = 8 KiB, 6 = 32 KiB, 7 = 64 KiB. -lh5- is the
 * common one, and is also what Crusher! writes for its "crushed" members.
 *
 * Unlike -lh1- (adaptive Huffman over a 4 KiB window) this is block based:
 * each block declares its own literal and position Huffman tables, themselves
 * coded with a small pre-table, and then that many symbols.
 *
 * @param input       Compressed bytes.
 * @param input_size  Length of @p input.
 * @param output      Receives the decoded bytes.
 * @param output_size Exact expected size; decoding stops there.
 * @param method      4, 5, 6 or 7.
 * @param written     Receives the number of bytes produced. May be NULL.
 * @return true when exactly @p output_size bytes were produced from a
 *         well-formed stream.
 */
XXFC_API bool xx_lzh5_decode_memory(const uint8_t *input, size_t input_size,
                                    uint8_t *output, size_t output_size,
                                    int method, size_t *written);

#ifdef __cplusplus
}
#endif
#endif
