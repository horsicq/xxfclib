/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/palm.c
 * PalmOS indexed bitmap v0/v1/v2: checked dimensions/stride/flags, bounded palette and complete uncompressed or byte-RLE raster extents. Original descriptor, palette and
 * encoded raster exported; direct-color, indirect storage, v3 and image chains declined. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_PALM_BITMAP_H
#define XX_PALM_BITMAP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_palm_bitmap {
    Abstractformat format;
} xx_palm_bitmap;
XXFC_API void xx_palm_bitmap_init(xx_palm_bitmap *, xx_io_device *, int64_t);
XXFC_API xx_palm_bitmap *xx_palm_bitmap_create(xx_io_device *, int64_t);
XXFC_API void xx_palm_bitmap_destroy(xx_palm_bitmap *);
XXFC_API void xx_palm_bitmap_free(xx_palm_bitmap *);
XXFC_API bool xx_palm_bitmap_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_palm_bitmap_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_palm_bitmap_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_palm_bitmap_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_palm_bitmap_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_palm_bitmap_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_palm_bitmap_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
