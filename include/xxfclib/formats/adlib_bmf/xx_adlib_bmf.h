/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_BMF_H
#define XX_ADLIB_BMF_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_bmf {Abstractformat format;} xx_adlib_bmf;
XXFC_API void xx_adlib_bmf_init(xx_adlib_bmf *,xx_io_device *,int64_t);
XXFC_API xx_adlib_bmf *xx_adlib_bmf_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_bmf_destroy(xx_adlib_bmf *);
XXFC_API void xx_adlib_bmf_free(xx_adlib_bmf *);
XXFC_API bool xx_adlib_bmf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_bmf_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
