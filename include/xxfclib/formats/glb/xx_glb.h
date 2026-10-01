/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_GLB_H
#define XX_GLB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_glb { Abstractformat format; } xx_glb;
XXFC_API void xx_glb_init(xx_glb *,xx_io_device *,int64_t);
XXFC_API xx_glb *xx_glb_create(xx_io_device *,int64_t);
XXFC_API void xx_glb_destroy(xx_glb *);
XXFC_API void xx_glb_free(xx_glb *);
XXFC_API bool xx_glb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_glb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
