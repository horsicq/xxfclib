/* SPDX-License-Identifier: MIT
 * Primary reference: https://imagemagick.org/miff/
 * MIFF1.0 uncompressed DirectClass RGB/sRGB8/16/32-bit images: complete bounded key/value descriptor, finite color metadata and exact interleaved raster rows; concatenated images, indexed palettes, profiles and compression declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_IMAGEMAGICK_MIFF_H
#define XX_IMAGEMAGICK_MIFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_imagemagick_miff {Abstractformat format;} xx_imagemagick_miff;
XXFC_API void xx_imagemagick_miff_init(xx_imagemagick_miff *,xx_io_device *,int64_t);
XXFC_API xx_imagemagick_miff *xx_imagemagick_miff_create(xx_io_device *,int64_t);
XXFC_API void xx_imagemagick_miff_destroy(xx_imagemagick_miff *);
XXFC_API void xx_imagemagick_miff_free(xx_imagemagick_miff *);
XXFC_API bool xx_imagemagick_miff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_imagemagick_miff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
