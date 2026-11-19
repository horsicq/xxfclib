/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reader-private declarations: shared by the reader's own units and, where
 * a DataStructs script needs reader state, src/datastructs/xx_xxds_readers.c.
 */
#ifndef XX_BZ2_PRIVATE_H
#define XX_BZ2_PRIVATE_H
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/archive/bz2/xx_bz2.h"
#include "xx_bz2_defs.h"
#include "xx_bzip2_internal.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

bool xx_bz2_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size);

#endif
