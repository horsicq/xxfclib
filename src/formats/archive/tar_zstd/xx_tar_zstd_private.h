/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reader-private declarations: shared by the reader's own units and, where
 * a DataStructs script needs reader state, src/datastructs/xx_xxds_readers.c.
 */
#ifndef XX_TAR_ZSTD_PRIVATE_H
#define XX_TAR_ZSTD_PRIVATE_H
#include "xxfclib/formats/archive/tar_zstd/xx_tar_zstd.h"
#include "../tar_common/xx_tar_common.h"
#include "../../../algo/zstd/xx_zstd_dec.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include <limits.h>
#include "xxfclib/data/xx_data.h"

xx_tar_common *xx_tar_zstd_common(xx_tar_zstd *tar_zstd,
                                         bool create);
bool xx_tar_zstd_decode(Abstractformat *outer,
                               xx_io_device *destination,
                               int64_t *compressed_size,
                               xx_pd_struct *pd);

#endif
