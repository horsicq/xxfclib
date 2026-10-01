/* SPDX-License-Identifier: MIT
 * Wire specification: https://teem.sourceforge.net/nrrd/format.html */
#ifndef XX_NRRD_H
#define XX_NRRD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nrrd { Abstractformat format; } xx_nrrd;
XXFC_API void xx_nrrd_init(xx_nrrd *,xx_io_device *,int64_t);
XXFC_API xx_nrrd *xx_nrrd_create(xx_io_device *,int64_t);
XXFC_API void xx_nrrd_destroy(xx_nrrd *);
XXFC_API void xx_nrrd_free(xx_nrrd *);
XXFC_API bool xx_nrrd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nrrd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
