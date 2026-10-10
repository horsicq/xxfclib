/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/edp/xx_edp.h"
#include "../ckpedp/xx_ckpedp_internal.h"
#include "xxfclib/memory/xx_memory.h"

void xx_edp_init(xx_edp *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_ckpedp_init(&reader->format, device, base_address, XX_FILE_TYPE_EDP);
}

xx_edp *xx_edp_create(xx_io_device *device, int64_t base_address)
{
    xx_edp *reader = (xx_edp *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_edp_init(reader, device, base_address);
    return reader;
}

void xx_edp_destroy(xx_edp *reader)
{
    if (reader) xx_ckpedp_destroy(&reader->format);
}

void xx_edp_free(xx_edp *reader)
{
    if (reader) {
        xx_edp_destroy(reader);
        xx_mem_free(reader);
    }
}

bool xx_edp_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    return format && format->file_type == XX_FILE_TYPE_EDP && xx_ckpedp_check_is_valid(format, pd);
}
