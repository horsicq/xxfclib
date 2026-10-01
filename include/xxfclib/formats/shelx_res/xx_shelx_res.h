/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.ase-lib.org/_modules/ase/io/res.html */
#ifndef XX_SHELX_RES_H
#define XX_SHELX_RES_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_shelx_res { Abstractformat format; } xx_shelx_res;
XXFC_API void xx_shelx_res_init(xx_shelx_res *,xx_io_device *,int64_t);
XXFC_API xx_shelx_res *xx_shelx_res_create(xx_io_device *,int64_t);
XXFC_API void xx_shelx_res_destroy(xx_shelx_res *);
XXFC_API void xx_shelx_res_free(xx_shelx_res *);
XXFC_API bool xx_shelx_res_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_shelx_res_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
