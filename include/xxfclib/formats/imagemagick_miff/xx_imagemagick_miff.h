/* SPDX-License-Identifier: MIT
 * Primary reference: https://imagemagick.org/miff/
 * MIFF1.0 uncompressed DirectClass RGB/sRGB8/16/32-bit images: complete bounded key/value descriptor, finite color metadata and exact interleaved raster rows;
 * concatenated images, indexed palettes, profiles and compression declined. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_IMAGEMAGICK_MIFF_H
#define XX_IMAGEMAGICK_MIFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_imagemagick_miff {
    Abstractformat format;
} xx_imagemagick_miff;
XXFC_API void xx_imagemagick_miff_init(xx_imagemagick_miff *, xx_io_device *, int64_t);
XXFC_API xx_imagemagick_miff *xx_imagemagick_miff_create(xx_io_device *, int64_t);
XXFC_API void xx_imagemagick_miff_destroy(xx_imagemagick_miff *);
XXFC_API void xx_imagemagick_miff_free(xx_imagemagick_miff *);
XXFC_API bool xx_imagemagick_miff_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_imagemagick_miff_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_imagemagick_miff_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_imagemagick_miff_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_imagemagick_miff_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_imagemagick_miff_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_imagemagick_miff_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
