/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.biosemi.com/faq/file_format.htm */
#ifndef XX_BIOMEDICAL_BDF_H
#define XX_BIOMEDICAL_BDF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_biomedical_bdf { Abstractformat format; } xx_biomedical_bdf;
XXFC_API void xx_biomedical_bdf_init(xx_biomedical_bdf *,xx_io_device *,int64_t);
XXFC_API xx_biomedical_bdf *xx_biomedical_bdf_create(xx_io_device *,int64_t);
XXFC_API void xx_biomedical_bdf_destroy(xx_biomedical_bdf *);
XXFC_API void xx_biomedical_bdf_free(xx_biomedical_bdf *);
XXFC_API bool xx_biomedical_bdf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_biomedical_bdf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_biomedical_bdf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_biomedical_bdf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_biomedical_bdf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_biomedical_bdf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_biomedical_bdf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
