/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/AFM-analysis/igor2/blob/master/igor2/binarywave.py */
#ifndef XX_IGOR_IBW_H
#define XX_IGOR_IBW_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_igor_ibw { Abstractformat format; } xx_igor_ibw;
XXFC_API void xx_igor_ibw_init(xx_igor_ibw *,xx_io_device *,int64_t);
XXFC_API xx_igor_ibw *xx_igor_ibw_create(xx_io_device *,int64_t);
XXFC_API void xx_igor_ibw_destroy(xx_igor_ibw *);
XXFC_API void xx_igor_ibw_free(xx_igor_ibw *);
XXFC_API bool xx_igor_ibw_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_igor_ibw_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
