/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_RAD_H
#define XX_ADLIB_RAD_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_rad {Abstractformat format;} xx_adlib_rad;
XXFC_API void xx_adlib_rad_init(xx_adlib_rad *,xx_io_device *,int64_t);
XXFC_API xx_adlib_rad *xx_adlib_rad_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_rad_destroy(xx_adlib_rad *);
XXFC_API void xx_adlib_rad_free(xx_adlib_rad *);
XXFC_API bool xx_adlib_rad_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_rad_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
