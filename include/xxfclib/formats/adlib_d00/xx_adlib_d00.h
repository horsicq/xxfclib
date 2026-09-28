/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_D00_H
#define XX_ADLIB_D00_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_d00 {Abstractformat format;} xx_adlib_d00;
XXFC_API void xx_adlib_d00_init(xx_adlib_d00 *,xx_io_device *,int64_t);
XXFC_API xx_adlib_d00 *xx_adlib_d00_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_d00_destroy(xx_adlib_d00 *);
XXFC_API void xx_adlib_d00_free(xx_adlib_d00 *);
XXFC_API bool xx_adlib_d00_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_d00_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
