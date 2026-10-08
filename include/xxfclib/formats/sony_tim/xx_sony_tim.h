/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://psx-spx.consoledev.net/cdromfileformats/#cdrom-file-video-tim-textures
 * PlayStation TIM version0 single-image files, 4/8-bit indexed and direct 16/24-bit pixels. Exports CLUT and encoded image planes with exact rectangle lengths; no rendering, mixed-depth or malformed game-specific variants.
 */
#ifndef XX_SONY_TIM_H
#define XX_SONY_TIM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_tim { Abstractformat format; } xx_sony_tim;
XXFC_API void xx_sony_tim_init(xx_sony_tim *,xx_io_device *,int64_t);
XXFC_API xx_sony_tim *xx_sony_tim_create(xx_io_device *,int64_t);
XXFC_API void xx_sony_tim_destroy(xx_sony_tim *);
XXFC_API void xx_sony_tim_free(xx_sony_tim *);
XXFC_API bool xx_sony_tim_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sony_tim_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sony_tim_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sony_tim_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sony_tim_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
