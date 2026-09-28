/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_ROL_H
#define XX_ADLIB_ROL_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_rol {Abstractformat format;} xx_adlib_rol;
XXFC_API void xx_adlib_rol_init(xx_adlib_rol *,xx_io_device *,int64_t);
XXFC_API xx_adlib_rol *xx_adlib_rol_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_rol_destroy(xx_adlib_rol *);
XXFC_API void xx_adlib_rol_free(xx_adlib_rol *);
XXFC_API bool xx_adlib_rol_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_rol_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
