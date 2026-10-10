/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/palladix_snd/xx_palladix_snd.h"
#include "xxfclib/memory/xx_memory.h"
void xx_palladix_snd_init(xx_palladix_snd *r, xx_io_device *d, int64_t base)
{
    xx_legacy_sound_driver_init(r, d, base, XX_FILE_TYPE_PALLADIX_SND, "snd");
}
xx_palladix_snd *xx_palladix_snd_create(xx_io_device *d, int64_t base)
{
    xx_palladix_snd *r = (xx_palladix_snd *)xx_mem_alloc(sizeof(*r));
    if (r) xx_palladix_snd_init(r, d, base);
    return r;
}
void xx_palladix_snd_destroy(xx_palladix_snd *r)
{
    xx_legacy_sound_driver_destroy(r);
}
void xx_palladix_snd_free(xx_palladix_snd *r)
{
    if (r) {
        xx_palladix_snd_destroy(r);
        xx_mem_free(r);
    }
}
