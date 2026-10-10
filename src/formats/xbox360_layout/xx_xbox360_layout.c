/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/xbox360_layout/xx_xbox360_layout.h"
#include "xxfclib/memory/xx_memory.h"
void xx_xbox360_layout_init(xx_xbox360_layout *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_XBOX360_LAYOUT, "img");
}
xx_xbox360_layout *xx_xbox360_layout_create(xx_io_device *d, int64_t base)
{
    xx_xbox360_layout *r = (xx_xbox360_layout *)xx_mem_alloc(sizeof(*r));
    if (r) xx_xbox360_layout_init(r, d, base);
    return r;
}
void xx_xbox360_layout_destroy(xx_xbox360_layout *r)
{
    xx_volume_destroy(r);
}
void xx_xbox360_layout_free(xx_xbox360_layout *r)
{
    if (r) {
        xx_xbox360_layout_destroy(r);
        xx_mem_free(r);
    }
}
