/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/NIFTI-Imaging/nifti_clib/master/nifti2/nifti2.h */
#ifndef XX_NIFTI2_H
#define XX_NIFTI2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nifti2 { Abstractformat format; } xx_nifti2;
XXFC_API void xx_nifti2_init(xx_nifti2 *,xx_io_device *,int64_t);
XXFC_API xx_nifti2 *xx_nifti2_create(xx_io_device *,int64_t);
XXFC_API void xx_nifti2_destroy(xx_nifti2 *);
XXFC_API void xx_nifti2_free(xx_nifti2 *);
XXFC_API bool xx_nifti2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nifti2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
