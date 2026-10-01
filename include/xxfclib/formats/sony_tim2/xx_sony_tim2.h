/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/GirianSeed/tim2/trunk/sample/tim2.h
 * TIM2 version4/format0, up to1024 single-level images with RGB16/RGB24/RGB32 or4/8-bit indexed texture data and matching CLUT. Exports encoded image/CLUT planes; no mipmaps, extended headers, swizzle reversal or rendering.
 */
#ifndef XX_SONY_TIM2_H
#define XX_SONY_TIM2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_tim2 { Abstractformat format; } xx_sony_tim2;
XXFC_API void xx_sony_tim2_init(xx_sony_tim2 *,xx_io_device *,int64_t);
XXFC_API xx_sony_tim2 *xx_sony_tim2_create(xx_io_device *,int64_t);
XXFC_API void xx_sony_tim2_destroy(xx_sony_tim2 *);
XXFC_API void xx_sony_tim2_free(xx_sony_tim2 *);
XXFC_API bool xx_sony_tim2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sony_tim2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
