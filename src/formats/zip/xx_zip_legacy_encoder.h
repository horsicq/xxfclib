/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_ZIP_LEGACY_ENCODER_H
#define XX_ZIP_LEGACY_ENCODER_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"

/* Internal ZIP writers for methods 1..6 and 10. Sources must expose their
 * size and support seeking. Input is buffered; the match finder and output
 * staging use bounded storage. Outputs are zero on failure. Method 6 writes
 * an 8 KiB dictionary and uncoded literals (general-purpose flags 0x0002). */
bool xx_zip_legacy_pack_source(xx_io_device *source, const char *source_path, uint16_t method, int level, int64_t *uncompressed_size, int64_t *compressed_size,
                               uint32_t *crc32, xx_io_device *destination, xx_pd_struct *progress);

#endif
