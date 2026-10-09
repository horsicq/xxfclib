/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/otb.c
 * Nokia OTA bitmap: checked information flags, short/extended positive dimensions, depth1 and exact MSB-packed rows; original descriptor and row bitmaps exported; extension chains declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_NOKIA_OTA_BITMAP_H
#define XX_NOKIA_OTA_BITMAP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nokia_ota_bitmap {Abstractformat format;} xx_nokia_ota_bitmap;
XXFC_API void xx_nokia_ota_bitmap_init(xx_nokia_ota_bitmap *,xx_io_device *,int64_t);
XXFC_API xx_nokia_ota_bitmap *xx_nokia_ota_bitmap_create(xx_io_device *,int64_t);
XXFC_API void xx_nokia_ota_bitmap_destroy(xx_nokia_ota_bitmap *);
XXFC_API void xx_nokia_ota_bitmap_free(xx_nokia_ota_bitmap *);
XXFC_API bool xx_nokia_ota_bitmap_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nokia_ota_bitmap_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nokia_ota_bitmap_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nokia_ota_bitmap_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nokia_ota_bitmap_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nokia_ota_bitmap_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nokia_ota_bitmap_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
