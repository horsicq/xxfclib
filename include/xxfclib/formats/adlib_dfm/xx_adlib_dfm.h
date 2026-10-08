/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_DFM_H
#define XX_ADLIB_DFM_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_dfm {Abstractformat format;} xx_adlib_dfm;
XXFC_API void xx_adlib_dfm_init(xx_adlib_dfm *,xx_io_device *,int64_t);
XXFC_API xx_adlib_dfm *xx_adlib_dfm_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_dfm_destroy(xx_adlib_dfm *);
XXFC_API void xx_adlib_dfm_free(xx_adlib_dfm *);
XXFC_API bool xx_adlib_dfm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_dfm_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adlib_dfm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adlib_dfm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adlib_dfm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
