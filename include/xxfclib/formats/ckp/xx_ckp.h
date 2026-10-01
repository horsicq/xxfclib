/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_CKP_H
#define XXFCLIB_FORMAT_CKP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ckp {
    Abstractformat format;
} xx_ckp;

XXFC_API void xx_ckp_init(xx_ckp *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_ckp *xx_ckp_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ckp_destroy(xx_ckp *reader);
XXFC_API void xx_ckp_free(xx_ckp *reader);
XXFC_API bool xx_ckp_check_is_valid(Abstractformat *format, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
#endif
