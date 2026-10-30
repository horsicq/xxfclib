/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_TAR_GZ_INTERNAL_H
#define XXFCLIB_TAR_GZ_INTERNAL_H

/* Reader helpers shared by xx_tar_gz.c and xx_tar_gz_datastructs.c. */

#include "xxfclib/formats/archive/tar_gz/xx_tar_gz.h"

#include "../tar_common/xx_tar_common.h"

xx_tar_common *xx_tar_gz_common(xx_tar_gz *tar_gz);
bool xx_tar_gz_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);

#endif /* XXFCLIB_TAR_GZ_INTERNAL_H */
