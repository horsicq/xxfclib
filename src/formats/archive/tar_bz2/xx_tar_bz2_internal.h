/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_TAR_BZ2_INTERNAL_H
#define XXFCLIB_TAR_BZ2_INTERNAL_H

/* Reader helpers shared by xx_tar_bz2.c and xx_tar_bz2_datastructs.c. */

#include "xxfclib/formats/archive/tar_bz2/xx_tar_bz2.h"

#include "xx_tar_common.h"

xx_tar_common *xx_tar_bz2_common(Abstractformat *self);
bool xx_tar_bz2_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);

#endif /* XXFCLIB_TAR_BZ2_INTERNAL_H */
