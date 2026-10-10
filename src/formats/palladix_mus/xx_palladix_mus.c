/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/palladix_mus/xx_palladix_mus.h"
#include "xxfclib/memory/xx_memory.h"
void xx_palladix_mus_init(xx_palladix_mus *r,xx_io_device *d,int64_t base) {
    xx_legacy_sound_driver_init(r,d,base,XX_FILE_TYPE_PALLADIX_MUS,"mus");
}
xx_palladix_mus *xx_palladix_mus_create(xx_io_device *d,int64_t base) {
    xx_palladix_mus *r=(xx_palladix_mus *)xx_mem_alloc(sizeof(*r));
    if (r) xx_palladix_mus_init(r,d,base);
    return r;
}
void xx_palladix_mus_destroy(xx_palladix_mus *r) { xx_legacy_sound_driver_destroy(r); }
void xx_palladix_mus_free(xx_palladix_mus *r) {
    if (r) { xx_palladix_mus_destroy(r);xx_mem_free(r); }
}
