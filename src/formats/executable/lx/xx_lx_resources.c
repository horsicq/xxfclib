/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/executable/lx/xx_lx.h"
#include "xxfclib/formats/xx_format_streams.h"
#include "../le/xx_linear_internal.h"

uint64_t xx_lx_get_number_of_resources(Abstractformat *format, xx_pd_struct *pd)
{
    return xx_linear_get_resource_count_impl(format, pd, XX_LX_SIGNATURE);
}

const StreamResources *xx_lx_get_stream_resources(void)
{
    static const StreamResources stream = {.get_number_of_resources = xx_lx_get_number_of_resources};
    return &stream;
}
