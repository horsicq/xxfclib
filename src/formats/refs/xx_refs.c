/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/refs/xx_refs.h"
#include "xxfclib/memory/xx_memory.h"
void xx_refs_init(xx_refs *r, xx_io_device *d, int64_t base)
{
    xx_volume_init(r, d, base, XX_FILE_TYPE_REFS, "img");
}
xx_refs *xx_refs_create(xx_io_device *d, int64_t base)
{
    xx_refs *r = (xx_refs *)xx_mem_alloc(sizeof(*r));
    if (r) xx_refs_init(r, d, base);
    return r;
}
void xx_refs_destroy(xx_refs *r)
{
    xx_volume_destroy(r);
}
void xx_refs_free(xx_refs *r)
{
    if (r) {
        xx_refs_destroy(r);
        xx_mem_free(r);
    }
}
