/* SPDX-License-Identifier: MIT */
#ifndef XX_KEN_KSM_H
#define XX_KEN_KSM_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_ken_ksm {Abstractformat format;} xx_ken_ksm;
XXFC_API void xx_ken_ksm_init(xx_ken_ksm *,xx_io_device *,int64_t);
XXFC_API xx_ken_ksm *xx_ken_ksm_create(xx_io_device *,int64_t);
XXFC_API void xx_ken_ksm_destroy(xx_ken_ksm *);
XXFC_API void xx_ken_ksm_free(xx_ken_ksm *);
XXFC_API bool xx_ken_ksm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ken_ksm_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ken_ksm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ken_ksm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ken_ksm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
