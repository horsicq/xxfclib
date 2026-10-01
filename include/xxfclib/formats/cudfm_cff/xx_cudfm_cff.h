/* SPDX-License-Identifier: MIT */
#ifndef XX_CUDFM_CFF_H
#define XX_CUDFM_CFF_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_cudfm_cff {Abstractformat format;} xx_cudfm_cff;
XXFC_API void xx_cudfm_cff_init(xx_cudfm_cff *,xx_io_device *,int64_t);
XXFC_API xx_cudfm_cff *xx_cudfm_cff_create(xx_io_device *,int64_t);
XXFC_API void xx_cudfm_cff_destroy(xx_cudfm_cff *);
XXFC_API void xx_cudfm_cff_free(xx_cudfm_cff *);
XXFC_API bool xx_cudfm_cff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cudfm_cff_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
