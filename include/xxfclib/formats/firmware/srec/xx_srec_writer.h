/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_srec_writer.h @brief Motorola S-record binary-to-text encoder. */

#ifndef XXFCLIB_FORMAT_SREC_WRITER_H
#define XXFCLIB_FORMAT_SREC_WRITER_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_SREC_DEFAULT_LINE_LENGTH 16U
#define XX_SREC_MAX_DATA_PER_LINE 250U

typedef enum xx_srec_format_e {
    XX_SREC_FORMAT_AUTO = 0, /**< S19 if <= 0xFFFF, S28 if <= 0xFFFFFF, else S37 */
    XX_SREC_FORMAT_S19 = 1,  /**< 16-bit address (S1 data, S9 termination) */
    XX_SREC_FORMAT_S28 = 2,  /**< 24-bit address (S2 data, S8 termination) */
    XX_SREC_FORMAT_S37 = 3   /**< 32-bit address (S3 data, S7 termination) */
} xx_srec_format;

/**
 * @brief Write binary data from a device to a Motorola S-record text output device with entry point.
 * Emits S0 header record, S1/S2/S3 data records, S5 (or S6) count record, and S9/S8/S7 termination.
 */
XXFC_API bool xx_srec_write_device_ex(xx_io_device *src, int64_t src_offset, size_t count,
                                      uint64_t base_address, unsigned line_length,
                                      xx_srec_format format, const char *header_text,
                                      uint64_t entry_point,
                                      xx_io_device *dest, xx_pd_struct *pd);

XXFC_API bool xx_srec_write_buffer_ex(const uint8_t *data, size_t size,
                                      uint64_t base_address, unsigned line_length,
                                      xx_srec_format format, const char *header_text,
                                      uint64_t entry_point,
                                      xx_io_device *dest, xx_pd_struct *pd);

XXFC_API bool xx_srec_write_device(xx_io_device *src, int64_t src_offset, size_t count,
                                   uint64_t base_address, unsigned line_length,
                                   xx_srec_format format, const char *header_text,
                                   xx_io_device *dest, xx_pd_struct *pd);

XXFC_API bool xx_srec_write_buffer(const uint8_t *data, size_t size,
                                   uint64_t base_address, unsigned line_length,
                                   xx_srec_format format, const char *header_text,
                                   xx_io_device *dest, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SREC_WRITER_H */
