/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_TAR_ZSTD_INTERNAL_H
#define XXFCLIB_TAR_ZSTD_INTERNAL_H

/* Reader helpers shared by xx_tar_zstd.c and xx_tar_zstd_datastructs.c. */

#include "xxfclib/formats/archive/tar_zstd/xx_tar_zstd.h"

#include "../tar_common/xx_tar_common.h"

xx_tar_common *xx_tar_zstd_common(xx_tar_zstd *tar_zstd, bool create);
bool xx_tar_zstd_decode(Abstractformat *outer, xx_io_device *destination,
                        int64_t *compressed_size, xx_pd_struct *pd);

#endif /* XXFCLIB_TAR_ZSTD_INTERNAL_H */
