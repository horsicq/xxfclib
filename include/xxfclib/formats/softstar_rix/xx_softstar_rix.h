/* SPDX-License-Identifier: MIT */
#ifndef XX_SOFTSTAR_RIX_H
#define XX_SOFTSTAR_RIX_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_softstar_rix {Abstractformat format;} xx_softstar_rix;
XXFC_API void xx_softstar_rix_init(xx_softstar_rix *,xx_io_device *,int64_t);
XXFC_API xx_softstar_rix *xx_softstar_rix_create(xx_io_device *,int64_t);
XXFC_API void xx_softstar_rix_destroy(xx_softstar_rix *);
XXFC_API void xx_softstar_rix_free(xx_softstar_rix *);
XXFC_API bool xx_softstar_rix_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_softstar_rix_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
