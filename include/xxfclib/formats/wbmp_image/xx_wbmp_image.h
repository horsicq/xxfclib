/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/wbmp.c
 * WAP WBMP type0: canonical bounded unsigned varints, positive dimensions, complete row-packed one-bit bitmap and exact EOF. Original descriptor and packed bitmap exported; extension types declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_WBMP_IMAGE_H
#define XX_WBMP_IMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wbmp_image {Abstractformat format;} xx_wbmp_image;
XXFC_API void xx_wbmp_image_init(xx_wbmp_image *,xx_io_device *,int64_t);
XXFC_API xx_wbmp_image *xx_wbmp_image_create(xx_io_device *,int64_t);
XXFC_API void xx_wbmp_image_destroy(xx_wbmp_image *);
XXFC_API void xx_wbmp_image_free(xx_wbmp_image *);
XXFC_API bool xx_wbmp_image_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_wbmp_image_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
