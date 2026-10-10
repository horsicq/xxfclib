/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/android_lp/xx_android_lp.h"
#include "xxfclib/memory/xx_memory.h"
void xx_android_lp_init(xx_android_lp *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_ANDROID_LP, "img");
}
xx_android_lp *xx_android_lp_create(xx_io_device *d, int64_t base)
{
    xx_android_lp *r = (xx_android_lp *)xx_mem_alloc(sizeof(*r));
    if (r) xx_android_lp_init(r, d, base);
    return r;
}
void xx_android_lp_destroy(xx_android_lp *r)
{
    xx_volume_destroy(r);
}
void xx_android_lp_free(xx_android_lp *r)
{
    if (r) {
        xx_android_lp_destroy(r);
        xx_mem_free(r);
    }
}
