/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/mtv.c
 * MTV raytracing image: complete ASCII dimensions and exact RGB24 rows; original descriptor and raster rows exported; image sequences declined. Signatureless fallback after structured readers.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_MTV_IMAGE_H
#define XX_MTV_IMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mtv_image {Abstractformat format;} xx_mtv_image;
XXFC_API void xx_mtv_image_init(xx_mtv_image *,xx_io_device *,int64_t);
XXFC_API xx_mtv_image *xx_mtv_image_create(xx_io_device *,int64_t);
XXFC_API void xx_mtv_image_destroy(xx_mtv_image *);
XXFC_API void xx_mtv_image_free(xx_mtv_image *);
XXFC_API bool xx_mtv_image_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mtv_image_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
