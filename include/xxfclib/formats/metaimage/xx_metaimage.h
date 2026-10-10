/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.itk.org/en/latest/learn/metaio.html */
#ifndef XX_METAIMAGE_H
#define XX_METAIMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_metaimage {
    Abstractformat format;
} xx_metaimage;
XXFC_API void xx_metaimage_init(xx_metaimage *, xx_io_device *, int64_t);
XXFC_API xx_metaimage *xx_metaimage_create(xx_io_device *, int64_t);
XXFC_API void xx_metaimage_destroy(xx_metaimage *);
XXFC_API void xx_metaimage_free(xx_metaimage *);
XXFC_API bool xx_metaimage_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_metaimage_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_metaimage_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_metaimage_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_metaimage_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_metaimage_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_metaimage_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
