/* SPDX-License-Identifier: MIT
 * Primary reference: https://imagemagick.org/magick-vector-graphics/
 * MVG graphics subset: complete balanced graphic contexts, finite viewbox/affine/transforms and checked color/stroke/text/geometric/path commands; original typed records
 * exported. Local drawing metadata retained without rendering or loading fonts; image/delegate/URL/external-resource commands declined. Bounded32MiB input,4096
 * components and bounded work.
 */
#ifndef XX_IMAGEMAGICK_MVG_H
#define XX_IMAGEMAGICK_MVG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_imagemagick_mvg {
    Abstractformat format;
} xx_imagemagick_mvg;
XXFC_API void xx_imagemagick_mvg_init(xx_imagemagick_mvg *, xx_io_device *, int64_t);
XXFC_API xx_imagemagick_mvg *xx_imagemagick_mvg_create(xx_io_device *, int64_t);
XXFC_API void xx_imagemagick_mvg_destroy(xx_imagemagick_mvg *);
XXFC_API void xx_imagemagick_mvg_free(xx_imagemagick_mvg *);
XXFC_API bool xx_imagemagick_mvg_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_imagemagick_mvg_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_imagemagick_mvg_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_imagemagick_mvg_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_imagemagick_mvg_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_imagemagick_mvg_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_imagemagick_mvg_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
