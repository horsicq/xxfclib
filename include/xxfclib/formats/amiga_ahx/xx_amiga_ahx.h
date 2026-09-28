/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AMIGA_AHX_H
#define XX_AMIGA_AHX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amiga_ahx { Abstractformat format; } xx_amiga_ahx;
XXFC_API void xx_amiga_ahx_init(xx_amiga_ahx *,xx_io_device *,int64_t);
XXFC_API xx_amiga_ahx *xx_amiga_ahx_create(xx_io_device *,int64_t);
XXFC_API void xx_amiga_ahx_destroy(xx_amiga_ahx *);
XXFC_API void xx_amiga_ahx_free(xx_amiga_ahx *);
XXFC_API bool xx_amiga_ahx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amiga_ahx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
