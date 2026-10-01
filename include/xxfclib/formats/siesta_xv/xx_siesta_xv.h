/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.ase-lib.org/_modules/ase/io/siesta.html */
#ifndef XX_SIESTA_XV_H
#define XX_SIESTA_XV_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_siesta_xv { Abstractformat format; } xx_siesta_xv;
XXFC_API void xx_siesta_xv_init(xx_siesta_xv *,xx_io_device *,int64_t);
XXFC_API xx_siesta_xv *xx_siesta_xv_create(xx_io_device *,int64_t);
XXFC_API void xx_siesta_xv_destroy(xx_siesta_xv *);
XXFC_API void xx_siesta_xv_free(xx_siesta_xv *);
XXFC_API bool xx_siesta_xv_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_siesta_xv_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
