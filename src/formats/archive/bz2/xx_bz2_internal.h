/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_BZ2_INTERNAL_H
#define XXFCLIB_BZ2_INTERNAL_H

/* Reader helpers shared by xx_bz2.c and xx_bz2_datastructs.c. */

#include "xxfclib/io/xx_io.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool xx_bz2_read_exact_at(xx_io_device *device, int64_t offset, void *buffer, size_t size);

#endif /* XXFCLIB_BZ2_INTERNAL_H */
