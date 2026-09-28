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
#endif
