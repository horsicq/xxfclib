/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/NIFTI-Imaging/nifti_clib/master/niftilib/nifti1.h */
#ifndef XX_NIFTI1_H
#define XX_NIFTI1_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nifti1 { Abstractformat format; } xx_nifti1;
XXFC_API void xx_nifti1_init(xx_nifti1 *,xx_io_device *,int64_t);
XXFC_API xx_nifti1 *xx_nifti1_create(xx_io_device *,int64_t);
XXFC_API void xx_nifti1_destroy(xx_nifti1 *);
XXFC_API void xx_nifti1_free(xx_nifti1 *);
XXFC_API bool xx_nifti1_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nifti1_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
