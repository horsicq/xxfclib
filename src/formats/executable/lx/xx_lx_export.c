/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/lx/xx_lx.h"
#include "xxfclib/formats/xx_format_streams.h"
#include "../le/xx_linear_internal.h"

uint64_t xx_lx_get_number_of_exports(Abstractformat *format, xx_pd_struct *pd)
{
    return xx_linear_get_export_count_impl(format, pd, XX_LX_SIGNATURE);
}

const StreamExport *xx_lx_get_stream_export(void)
{
    static const StreamExport stream = {.get_number_of_exports = xx_lx_get_number_of_exports};
    return &stream;
}
