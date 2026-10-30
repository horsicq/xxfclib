/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_TAR_LZ4_INTERNAL_H
#define XXFCLIB_TAR_LZ4_INTERNAL_H

/* Reader helpers shared by xx_tar_lz4.c and xx_tar_lz4_datastructs.c. */

#include "xxfclib/formats/archive/tar_lz4/xx_tar_lz4.h"

#include "../tar_common/xx_tar_common.h"

xx_tar_common *xx_tar_lz4_common(xx_tar_lz4 *tar_lz4, bool create);
bool xx_tar_lz4_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);

#endif /* XXFCLIB_TAR_LZ4_INTERNAL_H */
