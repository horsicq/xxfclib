/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_MTR_H
#define XX_ADLIB_MTR_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_mtr {Abstractformat format;} xx_adlib_mtr;
XXFC_API void xx_adlib_mtr_init(xx_adlib_mtr *,xx_io_device *,int64_t);
XXFC_API xx_adlib_mtr *xx_adlib_mtr_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_mtr_destroy(xx_adlib_mtr *);
XXFC_API void xx_adlib_mtr_free(xx_adlib_mtr *);
XXFC_API bool xx_adlib_mtr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_mtr_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
