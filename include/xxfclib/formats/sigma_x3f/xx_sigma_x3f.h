/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/LibRaw/LibRaw/master/internal/x3f_tools.h */
#ifndef XX_SIGMA_X3F_H
#define XX_SIGMA_X3F_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sigma_x3f { Abstractformat format; } xx_sigma_x3f;
XXFC_API void xx_sigma_x3f_init(xx_sigma_x3f *,xx_io_device *,int64_t);
XXFC_API xx_sigma_x3f *xx_sigma_x3f_create(xx_io_device *,int64_t);
XXFC_API void xx_sigma_x3f_destroy(xx_sigma_x3f *);
XXFC_API void xx_sigma_x3f_free(xx_sigma_x3f *);
XXFC_API bool xx_sigma_x3f_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sigma_x3f_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
