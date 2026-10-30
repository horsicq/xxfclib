/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_TAR_XZ_INTERNAL_H
#define XXFCLIB_TAR_XZ_INTERNAL_H

/* Reader helpers shared by xx_tar_xz.c and xx_tar_xz_datastructs.c. */

#include "xxfclib/formats/archive/tar_xz/xx_tar_xz.h"

#include "../tar_common/xx_tar_common.h"

xx_tar_common *xx_tar_xz_common(xx_tar_xz *tar_xz, bool create);
bool xx_tar_xz_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);

#endif /* XXFCLIB_TAR_XZ_INTERNAL_H */
