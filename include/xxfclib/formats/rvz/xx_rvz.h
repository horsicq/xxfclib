/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_RVZ_H
#define XXFCLIB_FORMAT_RVZ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Dolphin RVZ v1 GameCube images, exposed as a reconstructed ISO member. */
typedef struct xx_rvz {
    Abstractformat format;
} xx_rvz;

XXFC_API void xx_rvz_init(xx_rvz *reader, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_rvz *xx_rvz_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rvz_destroy(xx_rvz *reader);
XXFC_API void xx_rvz_free(xx_rvz *reader);
XXFC_API bool xx_rvz_check_is_valid(Abstractformat *format, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
#endif
