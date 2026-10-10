/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/qnx4/xx_qnx4.h"
#include "xxfclib/memory/xx_memory.h"
void xx_qnx4_init(xx_qnx4 *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_QNX4, "img");
}
xx_qnx4 *xx_qnx4_create(xx_io_device *d, int64_t base)
{
    xx_qnx4 *r = (xx_qnx4 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_qnx4_init(r, d, base);
    return r;
}
void xx_qnx4_destroy(xx_qnx4 *r)
{
    xx_volume_destroy(r);
}
void xx_qnx4_free(xx_qnx4 *r)
{
    if (r) {
        xx_qnx4_destroy(r);
        xx_mem_free(r);
    }
}
