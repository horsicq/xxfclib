/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xx_scan_types_internal.h"
#include "xxfclib/formats/executable/dos16m/xx_dos16m.h"

xx_file_type_t xx_scan_type_detect_msdos(xx_io_device *device)
{
    xx_dos4g dos4g;
    xx_dos16m dos16m;
    bool valid;
    xx_dos4g_init(&dos4g, device, 0);
    valid = xx_dos4g_check_is_valid(&dos4g.format, NULL);
    xx_dos4g_destroy(&dos4g);
    if (valid) return XX_FILE_TYPE_DOS4G;
    xx_dos16m_init(&dos16m, device, 0);
    valid = xx_dos16m_check_is_valid(&dos16m.format, NULL);
    xx_dos16m_destroy(&dos16m);
    return valid ? XX_FILE_TYPE_DOS16M : XX_FILE_TYPE_MSDOS;
}
