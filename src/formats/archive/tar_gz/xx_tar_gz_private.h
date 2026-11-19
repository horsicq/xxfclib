/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reader-private declarations: shared by the reader's own units and, where
 * a DataStructs script needs reader state, src/datastructs/xx_xxds_readers.c.
 */
#ifndef XX_TAR_GZ_PRIVATE_H
#define XX_TAR_GZ_PRIVATE_H
#include "xxfclib/formats/archive/tar_gz/xx_tar_gz.h"
#include "../tar_common/xx_tar_common.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/formats/archive/gz/xx_gz.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>
#include "xxfclib/data/xx_data.h"

xx_tar_common *xx_tar_gz_common(xx_tar_gz *tar_gz);
bool xx_tar_gz_decode(Abstractformat *outer, xx_io_device *destination, int64_t *compressed_size, xx_pd_struct *pd);

#endif
