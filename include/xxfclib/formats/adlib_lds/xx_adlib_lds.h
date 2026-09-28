/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_LDS_H
#define XX_ADLIB_LDS_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_lds {Abstractformat format;} xx_adlib_lds;
XXFC_API void xx_adlib_lds_init(xx_adlib_lds *,xx_io_device *,int64_t);
XXFC_API xx_adlib_lds *xx_adlib_lds_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_lds_destroy(xx_adlib_lds *);
XXFC_API void xx_adlib_lds_free(xx_adlib_lds *);
XXFC_API bool xx_adlib_lds_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_lds_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
