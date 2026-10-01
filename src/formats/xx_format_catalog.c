/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/xx_format.h"

#include <string.h>

xx_list_t *xx_format_get_supported_file_types(void)
{
    xx_list_t *types = xx_list_create(sizeof(xx_file_type_t), NULL);
    int value;
#ifdef XXFC_FORMATS_ONLY
    const int last = XX_FILE_TYPE_PDF;
#else
    const int last = XX_FILE_TYPE_DIE_MUSIC_YM3812OPL2REGLOG;
#endif

    if (!types) return NULL;
    for (value = XX_FILE_TYPE_BINARY; value <= last; ++value) {
        xx_file_type_t type = (xx_file_type_t)value;
        if (strcmp(xx_format_file_type_to_string(type), "UNKNOWN") == 0) continue;
        if (!xx_list_append(types, &type)) {
            xx_list_destroy(types);
            return NULL;
        }
    }
    return types;
}
