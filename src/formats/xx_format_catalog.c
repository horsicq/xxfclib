/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/formats/xx_format.h"

#include <string.h>

xx_list_t *xx_format_get_supported_file_types(void)
{
    xx_list_t *types = xx_list_create(sizeof(xx_file_type_t), NULL);
#if !XFU_FORMAT_SUPPORT_FULL
    static const xx_file_type_t compiled_types[] = {
#define XX_SUPPORTED_FORMAT(type, name, label) XX_FILE_TYPE_##type,
#include "xxfclib/formats/xx_format_support_rows.inc"
#undef XX_SUPPORTED_FORMAT
    };
    size_t i;
    if (!types) return NULL;
    for (i = 0; i < sizeof(compiled_types) / sizeof(compiled_types[0]); ++i) {
        if (!xx_list_append(types, &compiled_types[i])) {
            xx_list_destroy(types);
            return NULL;
        }
    }
#else
    int value;
    const int last = XX_FILE_TYPE_DWC;

    if (!types) return NULL;
    for (value = XX_FILE_TYPE_BINARY; value <= last; ++value) {
#ifdef XXFC_FORMATS_ONLY
        if (value >= 1600 && value <= XX_FILE_TYPE_DIE_MUSIC_YM3812OPL2REGLOG) continue;
#endif
        xx_file_type_t type = (xx_file_type_t)value;
        if (strcmp(xx_format_file_type_to_string(type), "UNKNOWN") == 0) continue;
        if (!xx_list_append(types, &type)) {
            xx_list_destroy(types);
            return NULL;
        }
    }
#endif
    return types;
}
