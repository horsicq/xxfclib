/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_ZIP_INTERNAL_H
#define XXFCLIB_ZIP_INTERNAL_H

/* Reader helpers shared by xx_zip.c and xx_zip_datastructs.c. */

#include "xxfclib/formats/archive/zip/xx_zip.h"

#include <stdbool.h>
#include <stdint.h>

bool xx_zip_parse_cd_entry(xx_zip *zip, xx_io_device *device, int64_t dev_size, int64_t curr_offset, xx_archive_record *rec, int64_t *entry_size_out);

#endif /* XXFCLIB_ZIP_INTERNAL_H */
