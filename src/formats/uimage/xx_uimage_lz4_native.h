/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_UIMAGE_LZ4_NATIVE_H
#define XX_UIMAGE_LZ4_NATIVE_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/formats/xx_format.h"

/* Decode a bounded IH_COMP_LZ4 frame whose uImage header has no output size.
 * No destination bytes are written unless the complete frame validates. */
bool xx_uimage_lz4_decode_device(xx_io_device *source, int64_t offset, int64_t packed_size, xx_io_device *destination, size_t max_output, xx_pd_struct *pd);

#endif
