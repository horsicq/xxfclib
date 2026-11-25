/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/le/xx_le.h"
#include "xxfclib/formats/xx_format_streams.h"
#include "xx_linear_internal.h"

uint64_t xx_linear_get_import_count_impl(Abstractformat *format, xx_pd_struct *pd, uint16_t signature)
{
    return format && (format->base_info_handled || xx_linear_handle_impl(format, pd, signature)) ? format->number_of_imports : 0U;
}

uint64_t xx_le_get_number_of_imports(Abstractformat *format, xx_pd_struct *pd)
{
    return xx_linear_get_import_count_impl(format, pd, XX_LE_SIGNATURE);
}

const StreamImport *xx_le_get_stream_import(void)
{
    static const StreamImport stream = {.get_number_of_imports = xx_le_get_number_of_imports};
    return &stream;
}
