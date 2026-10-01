/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/LibRaw/LibRaw/master/src/metadata/minolta.cpp */
#ifndef XX_MINOLTA_MRW_H
#define XX_MINOLTA_MRW_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_minolta_mrw { Abstractformat format; } xx_minolta_mrw;
XXFC_API void xx_minolta_mrw_init(xx_minolta_mrw *,xx_io_device *,int64_t);
XXFC_API xx_minolta_mrw *xx_minolta_mrw_create(xx_io_device *,int64_t);
XXFC_API void xx_minolta_mrw_destroy(xx_minolta_mrw *);
XXFC_API void xx_minolta_mrw_free(xx_minolta_mrw *);
XXFC_API bool xx_minolta_mrw_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_minolta_mrw_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
