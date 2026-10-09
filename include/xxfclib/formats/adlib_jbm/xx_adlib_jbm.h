/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_JBM_H
#define XX_ADLIB_JBM_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_jbm {Abstractformat format;} xx_adlib_jbm;
XXFC_API void xx_adlib_jbm_init(xx_adlib_jbm *,xx_io_device *,int64_t);
XXFC_API xx_adlib_jbm *xx_adlib_jbm_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_jbm_destroy(xx_adlib_jbm *);
XXFC_API void xx_adlib_jbm_free(xx_adlib_jbm *);
XXFC_API bool xx_adlib_jbm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_jbm_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adlib_jbm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_adlib_jbm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adlib_jbm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adlib_jbm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_adlib_jbm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
