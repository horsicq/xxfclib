/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/pict.c
 * Apple QuickDraw PICTv2 bounded bitmap subset: checked frame/version/extended-header/rectangular clip and DirectBitsRect RGB32 PackBits rows with exact EndPic/EOF. Original typed opcodes and decoded RGB raster exported; legacy picSize informational, file header when present must be512 zero bytes. JPEG/regions/vector/unknown opcodes declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_APPLE_PICT_H
#define XX_APPLE_PICT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_apple_pict {Abstractformat format;} xx_apple_pict;
XXFC_API void xx_apple_pict_init(xx_apple_pict *,xx_io_device *,int64_t);
XXFC_API xx_apple_pict *xx_apple_pict_create(xx_io_device *,int64_t);
XXFC_API void xx_apple_pict_destroy(xx_apple_pict *);
XXFC_API void xx_apple_pict_free(xx_apple_pict *);
XXFC_API bool xx_apple_pict_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_pict_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_apple_pict_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_apple_pict_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_apple_pict_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
