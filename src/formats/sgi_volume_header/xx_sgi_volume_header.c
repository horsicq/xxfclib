/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/sgi_volume_header/xx_sgi_volume_header.h"
#include "xxfclib/memory/xx_memory.h"
void xx_sgi_volume_header_init(xx_sgi_volume_header *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_SGI_VOLUME_HEADER, "img");
}
xx_sgi_volume_header *xx_sgi_volume_header_create(xx_io_device *d, int64_t base)
{
    xx_sgi_volume_header *r = (xx_sgi_volume_header *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sgi_volume_header_init(r, d, base);
    return r;
}
void xx_sgi_volume_header_destroy(xx_sgi_volume_header *r)
{
    xx_volume_destroy(r);
}
void xx_sgi_volume_header_free(xx_sgi_volume_header *r)
{
    if (r) {
        xx_sgi_volume_header_destroy(r);
        xx_mem_free(r);
    }
}
