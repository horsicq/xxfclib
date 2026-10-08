/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_XSM_H
#define XX_ADLIB_XSM_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_xsm {Abstractformat format;} xx_adlib_xsm;
XXFC_API void xx_adlib_xsm_init(xx_adlib_xsm *,xx_io_device *,int64_t);
XXFC_API xx_adlib_xsm *xx_adlib_xsm_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_xsm_destroy(xx_adlib_xsm *);
XXFC_API void xx_adlib_xsm_free(xx_adlib_xsm *);
XXFC_API bool xx_adlib_xsm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_xsm_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adlib_xsm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adlib_xsm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adlib_xsm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
