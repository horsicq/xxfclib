/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/theochem/iodata/blob/master/iodata/formats/fchk.py */
#ifndef XX_GAUSSIAN_FCHK_H
#define XX_GAUSSIAN_FCHK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gaussian_fchk { Abstractformat format; } xx_gaussian_fchk;
XXFC_API void xx_gaussian_fchk_init(xx_gaussian_fchk *,xx_io_device *,int64_t);
XXFC_API xx_gaussian_fchk *xx_gaussian_fchk_create(xx_io_device *,int64_t);
XXFC_API void xx_gaussian_fchk_destroy(xx_gaussian_fchk *);
XXFC_API void xx_gaussian_fchk_free(xx_gaussian_fchk *);
XXFC_API bool xx_gaussian_fchk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gaussian_fchk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
