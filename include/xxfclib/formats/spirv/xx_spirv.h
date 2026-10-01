/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SPIRV_H
#define XX_SPIRV_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_spirv { Abstractformat format; } xx_spirv;
XXFC_API void xx_spirv_init(xx_spirv *,xx_io_device *,int64_t);
XXFC_API xx_spirv *xx_spirv_create(xx_io_device *,int64_t);
XXFC_API void xx_spirv_destroy(xx_spirv *);
XXFC_API void xx_spirv_free(xx_spirv *);
XXFC_API bool xx_spirv_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_spirv_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
