/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMATS_KA_XX_KA_H
#define XXFCLIB_FORMATS_KA_XX_KA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** KA Archive: absolute-offset table of stored members. */
typedef struct xx_ka {
    Abstractformat format;
} xx_ka;

XXFC_API void xx_ka_init(xx_ka *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_ka *xx_ka_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ka_destroy(xx_ka *archive);
XXFC_API void xx_ka_free(xx_ka *archive);
XXFC_API bool xx_ka_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_ka_handle_base_info(Abstractformat *format, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
#endif
