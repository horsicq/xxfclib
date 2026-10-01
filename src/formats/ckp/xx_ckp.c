/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#include "xxfclib/formats/ckp/xx_ckp.h"
#include "../ckpedp/xx_ckpedp_internal.h"
#include "xxfclib/memory/xx_memory.h"

void xx_ckp_init(xx_ckp *reader, xx_io_device *device, int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    xx_ckpedp_init(&reader->format, device, base_address, XX_FILE_TYPE_CKP);
}

xx_ckp *xx_ckp_create(xx_io_device *device, int64_t base_address) {
    xx_ckp *reader = (xx_ckp *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_ckp_init(reader, device, base_address);
    return reader;
}

void xx_ckp_destroy(xx_ckp *reader) {
    if (reader) xx_ckpedp_destroy(&reader->format);
}

void xx_ckp_free(xx_ckp *reader) {
    if (reader) {
        xx_ckp_destroy(reader);
        xx_mem_free(reader);
    }
}

bool xx_ckp_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return format && format->file_type == XX_FILE_TYPE_CKP &&
           xx_ckpedp_check_is_valid(format, pd);
}
