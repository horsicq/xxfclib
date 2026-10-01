/* SPDX-License-Identifier: MIT
 * Primary reference: https://imagemagick.org/magick-vector-graphics/
 * MVG graphics subset: complete balanced graphic contexts, finite viewbox/affine/transforms and checked color/stroke/text/geometric/path commands; original typed records exported. Local drawing metadata retained without rendering or loading fonts; image/delegate/URL/external-resource commands declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_IMAGEMAGICK_MVG_H
#define XX_IMAGEMAGICK_MVG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_imagemagick_mvg {Abstractformat format;} xx_imagemagick_mvg;
XXFC_API void xx_imagemagick_mvg_init(xx_imagemagick_mvg *,xx_io_device *,int64_t);
XXFC_API xx_imagemagick_mvg *xx_imagemagick_mvg_create(xx_io_device *,int64_t);
XXFC_API void xx_imagemagick_mvg_destroy(xx_imagemagick_mvg *);
XXFC_API void xx_imagemagick_mvg_free(xx_imagemagick_mvg *);
XXFC_API bool xx_imagemagick_mvg_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_imagemagick_mvg_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
