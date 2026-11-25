/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/avs.c
 * AVS X image: positive bounded big-endian dimensions and exact8-bit ARGB raster; descriptor and original ARGB row components exported; image sequences declined.
 * Signatureless fallback after structured readers. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_AVS_IMAGE_H
#define XX_AVS_IMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_avs_image {
    Abstractformat format;
} xx_avs_image;
XXFC_API void xx_avs_image_init(xx_avs_image *, xx_io_device *, int64_t);
XXFC_API xx_avs_image *xx_avs_image_create(xx_io_device *, int64_t);
XXFC_API void xx_avs_image_destroy(xx_avs_image *);
XXFC_API void xx_avs_image_free(xx_avs_image *);
XXFC_API bool xx_avs_image_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_avs_image_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_avs_image_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_avs_image_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_avs_image_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_avs_image_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_avs_image_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
