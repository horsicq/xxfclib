/* SPDX-License-Identifier: MIT. Bounded native cri_ahx reader. */
#ifndef XX_CRI_AHX_H
#define XX_CRI_AHX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cri_ahx { Abstractformat format; } xx_cri_ahx;
XXFC_API void xx_cri_ahx_init(xx_cri_ahx *,xx_io_device *,int64_t);
XXFC_API xx_cri_ahx *xx_cri_ahx_create(xx_io_device *,int64_t);
XXFC_API void xx_cri_ahx_destroy(xx_cri_ahx *);
XXFC_API void xx_cri_ahx_free(xx_cri_ahx *);
XXFC_API bool xx_cri_ahx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cri_ahx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
