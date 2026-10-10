/* SPDX-License-Identifier: MIT */
#ifndef XX_CUDFM_CFF_H
#define XX_CUDFM_CFF_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_cudfm_cff {
    Abstractformat format;
} xx_cudfm_cff;
XXFC_API void xx_cudfm_cff_init(xx_cudfm_cff *, xx_io_device *, int64_t);
XXFC_API xx_cudfm_cff *xx_cudfm_cff_create(xx_io_device *, int64_t);
XXFC_API void xx_cudfm_cff_destroy(xx_cudfm_cff *);
XXFC_API void xx_cudfm_cff_free(xx_cudfm_cff *);
XXFC_API bool xx_cudfm_cff_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_cudfm_cff_handle_base_info(Abstractformat *, xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cudfm_cff_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cudfm_cff_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cudfm_cff_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cudfm_cff_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cudfm_cff_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
