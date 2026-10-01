/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_ALGO_QUANTUM_H
#define XXFCLIB_ALGO_QUANTUM_H

#include "xxfclib/xxfc_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

XXFC_API bool xx_quantum_cab_decode(const uint8_t *const *blocks,
                                    const size_t *block_sizes,
                                    const size_t *plain_sizes,
                                    size_t block_count,
                                    unsigned window_bits,
                                    uint8_t *output,
                                    size_t output_size,
                                    size_t *written);

/* Standalone DS archive: one continuous coder and model state.  A raw
 * 16-bit rotating checksum follows each uncompressed member. */
XXFC_API bool xx_quantum_archive_decode(const uint8_t *data,
                                        size_t data_size,
                                        const size_t *member_sizes,
                                        size_t member_count,
                                        unsigned window_bits,
                                        uint8_t *output,
                                        size_t output_size,
                                        size_t *written);

/* Versions below 0x17 use reversed selectors, weighted models and a checksum
 * stored in each directory record rather than a trailer in the bitstream. */
XXFC_API bool xx_quantum_archive_decode_old(const uint8_t *data,
                                            size_t data_size,
                                            const size_t *member_sizes,
                                            const uint16_t *checksums,
                                            size_t member_count,
                                            unsigned window_bits,
                                            uint8_t *output,
                                            size_t output_size,
                                            size_t *written);

#ifdef __cplusplus
}
#endif
#endif
