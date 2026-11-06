/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"
#include "xxfclib/formats/executable/ne/xx_ne.h"
#include "xxfclib/formats/executable/le/xx_le.h"
#include "xxfclib/formats/executable/lx/xx_lx.h"

xx_file_type_t xx_scan_type_detect_ne_le_lx(xx_io_device *device)
{
    xx_ne ne;
    xx_le le;
    xx_lx lx;
    bool valid;
    xx_ne_init(&ne, device, 0);
    valid = xx_ne_check_is_valid(&ne.format, NULL);
    xx_ne_destroy(&ne);
    if (valid) return XX_FILE_TYPE_NE;
    xx_le_init(&le, device, 0);
    valid = xx_le_check_is_valid(&le.format, NULL);
    xx_le_destroy(&le);
    if (valid) return XX_FILE_TYPE_LE;
    xx_lx_init(&lx, device, 0);
    valid = xx_lx_check_is_valid(&lx.format, NULL);
    xx_lx_destroy(&lx);
    return valid ? XX_FILE_TYPE_LX : XX_FILE_TYPE_UNKNOWN;
}
