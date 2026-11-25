/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/firmware/vxworks/xx_vxworks.h"
#include "xxfclib/formats/xx_format_streams.h"

uint64_t xx_vxworks_get_number_of_metadata(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_vxworks *)self)->number_of_symbols;
}

const StreamMetadata *xx_vxworks_get_stream_metadata(void)
{
    static const StreamMetadata stream = {.get_number_of_metadata = xx_vxworks_get_number_of_metadata};
    return &stream;
}
