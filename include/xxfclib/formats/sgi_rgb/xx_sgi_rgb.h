/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://ftp.zx.net.nz/pub/archive/ftp.sgi.com/graphics/grafica/sgiimage.html
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_SGI_RGB_H
#define XX_SGI_RGB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sgi_rgb { Abstractformat format; } xx_sgi_rgb;
XXFC_API void xx_sgi_rgb_init(xx_sgi_rgb *,xx_io_device *,int64_t);
XXFC_API xx_sgi_rgb *xx_sgi_rgb_create(xx_io_device *,int64_t);
XXFC_API void xx_sgi_rgb_destroy(xx_sgi_rgb *);
XXFC_API void xx_sgi_rgb_free(xx_sgi_rgb *);
XXFC_API bool xx_sgi_rgb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sgi_rgb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
