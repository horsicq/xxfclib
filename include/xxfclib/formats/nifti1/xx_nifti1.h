/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/NIFTI-Imaging/nifti_clib/master/niftilib/nifti1.h */
#ifndef XX_NIFTI1_H
#define XX_NIFTI1_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nifti1 { Abstractformat format; } xx_nifti1;
XXFC_API void xx_nifti1_init(xx_nifti1 *,xx_io_device *,int64_t);
XXFC_API xx_nifti1 *xx_nifti1_create(xx_io_device *,int64_t);
XXFC_API void xx_nifti1_destroy(xx_nifti1 *);
XXFC_API void xx_nifti1_free(xx_nifti1 *);
XXFC_API bool xx_nifti1_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nifti1_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nifti1_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nifti1_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nifti1_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nifti1_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nifti1_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
