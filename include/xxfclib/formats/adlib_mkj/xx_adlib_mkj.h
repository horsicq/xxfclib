/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_MKJ_H
#define XX_ADLIB_MKJ_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_mkj {Abstractformat format;} xx_adlib_mkj;
XXFC_API void xx_adlib_mkj_init(xx_adlib_mkj *,xx_io_device *,int64_t);
XXFC_API xx_adlib_mkj *xx_adlib_mkj_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_mkj_destroy(xx_adlib_mkj *);
XXFC_API void xx_adlib_mkj_free(xx_adlib_mkj *);
XXFC_API bool xx_adlib_mkj_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_mkj_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adlib_mkj_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adlib_mkj_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adlib_mkj_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
