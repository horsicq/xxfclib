/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#include "xxfclib/formats/parsec_dtc/xx_parsec_dtc.h"
#include "xxfclib/memory/xx_memory.h"
void xx_parsec_dtc_init(xx_parsec_dtc *r,xx_io_device *d,int64_t base) {
    xx_legacy_sound_driver_init(r,d,base,XX_FILE_TYPE_PARSEC_DTC,"dtc");
}
xx_parsec_dtc *xx_parsec_dtc_create(xx_io_device *d,int64_t base) {
    xx_parsec_dtc *r=(xx_parsec_dtc *)xx_mem_alloc(sizeof(*r));
    if (r) xx_parsec_dtc_init(r,d,base);
    return r;
}
void xx_parsec_dtc_destroy(xx_parsec_dtc *r) { xx_legacy_sound_driver_destroy(r); }
void xx_parsec_dtc_free(xx_parsec_dtc *r) {
    if (r) { xx_parsec_dtc_destroy(r);xx_mem_free(r); }
}
