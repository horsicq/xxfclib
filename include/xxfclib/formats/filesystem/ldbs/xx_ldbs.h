/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_LDBS_H
#define XX_LDBS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Native LDBS DSK\2 block-store reader. The bounded raw-image variant
 * requires complete, regular FM/MFM tracks with healthy, single-copy or
 * explicitly blank sectors. Other physical/protected layouts are rejected. */
typedef struct xx_ldbs_s {
    Abstractformat format;
} xx_ldbs;
XXFC_API void xx_ldbs_init(xx_ldbs *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_ldbs *xx_ldbs_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ldbs_destroy(xx_ldbs *reader);
XXFC_API void xx_ldbs_free(xx_ldbs *reader);
XXFC_API bool xx_ldbs_unpack_to_device(xx_ldbs *reader, xx_io_device *destination, xx_pd_struct *pd);
#ifdef __cplusplus
}
#endif
#endif
