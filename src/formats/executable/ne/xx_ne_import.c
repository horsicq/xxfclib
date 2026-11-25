/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/ne/xx_ne.h"
#include "xxfclib/formats/xx_format_streams.h"

uint64_t xx_ne_get_number_of_imports(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_ne_handle_base_info(format, pd)) ? format->number_of_imports : 0U;
}

const StreamImport *xx_ne_get_stream_import(void)
{
    static const StreamImport stream = {.get_number_of_imports = xx_ne_get_number_of_imports};
    return &stream;
}
