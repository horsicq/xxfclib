/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ihex_writer.h @brief Intel HEX (IHex) binary-to-text encoder. */

#ifndef XXFCLIB_FORMAT_IHEX_WRITER_H
#define XXFCLIB_FORMAT_IHEX_WRITER_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_IHEX_DEFAULT_LINE_LENGTH 16U
#define XX_IHEX_MAX_DATA_PER_LINE 255U

/**
 * @brief Write binary data from a device to an Intel HEX text output device.
 * Emits I8HEX when highest address <= 0xFFFF; emits I32HEX (Type 04 records)
 * when addressing requires 32 bits or crosses 64 KiB boundaries.
 * Ends with EOF record (:00000001FF).
 */
XXFC_API bool xx_ihex_write_device(xx_io_device *src, int64_t src_offset, size_t count,
                                   uint64_t base_address, unsigned line_length,
                                   xx_io_device *dest, xx_pd_struct *pd);

XXFC_API bool xx_ihex_write_buffer(const uint8_t *data, size_t size,
                                   uint64_t base_address, unsigned line_length,
                                   xx_io_device *dest, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IHEX_WRITER_H */
