/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/le/xx_le.h"
#include "xxfclib/formats/xx_format_streams.h"
#include "xx_linear_internal.h"

uint64_t xx_linear_get_export_count_impl(Abstractformat *format, xx_pd_struct *pd, uint16_t signature)
{
    return format && (format->base_info_handled || xx_linear_handle_impl(format, pd, signature)) ? format->number_of_exports : 0U;
}

uint64_t xx_le_get_number_of_exports(Abstractformat *format, xx_pd_struct *pd)
{
    return xx_linear_get_export_count_impl(format, pd, XX_LE_SIGNATURE);
}

const StreamExport *xx_le_get_stream_export(void)
{
    static const StreamExport stream = {.get_number_of_exports = xx_le_get_number_of_exports};
    return &stream;
}
