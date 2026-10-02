/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_7ZIP_STREAM_H
#define XX_7ZIP_STREAM_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

/* Private coder adapters. Devices are borrowed and remain open; output is
 * sequential and must be staged by the caller until size/CRC validation.
 * Folder-sized allocations are confined to codecs with memory-only APIs,
 * where address space and allocation failure remain the limiting factors. */
bool xx_7zip_stream_decode(uint64_t method, const uint8_t *properties,
                            size_t properties_size, xx_io_device *source,
                            int64_t source_offset, uint64_t compressed_size,
                            uint64_t expected_size, xx_io_device *destination,
                            const uint8_t *password_utf16le, size_t password_size,
                            xx_pd_struct *pd);
/* Each independent input device/view starts at offset zero. */
bool xx_7zip_stream_bcj2(xx_io_device *const sources[4], const uint64_t sizes[4],
                          const uint8_t *properties, size_t properties_size,
                          uint64_t expected_size, xx_io_device *destination,
                          xx_pd_struct *pd);

#endif /* XX_7ZIP_STREAM_H */
