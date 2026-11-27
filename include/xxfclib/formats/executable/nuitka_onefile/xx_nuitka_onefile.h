/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_NUITKA_ONEFILE_H
#define XX_NUITKA_ONEFILE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_nuitka_onefile {
    Abstractformat format;
} xx_nuitka_onefile;

XXFC_API void xx_nuitka_onefile_init(xx_nuitka_onefile *reader, xx_io_device *device, int64_t base);
XXFC_API xx_nuitka_onefile *xx_nuitka_onefile_create(xx_io_device *device, int64_t base);
XXFC_API void xx_nuitka_onefile_destroy(xx_nuitka_onefile *reader);
XXFC_API void xx_nuitka_onefile_free(xx_nuitka_onefile *reader);
XXFC_API bool xx_nuitka_onefile_check_is_valid(Abstractformat *format, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif
