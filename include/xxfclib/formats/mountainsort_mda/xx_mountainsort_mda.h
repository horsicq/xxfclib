/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/magland/mountainlab_pytools */
#ifndef XX_MOUNTAINSORT_MDA_H
#define XX_MOUNTAINSORT_MDA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mountainsort_mda { Abstractformat format; } xx_mountainsort_mda;
XXFC_API void xx_mountainsort_mda_init(xx_mountainsort_mda *,xx_io_device *,int64_t);
XXFC_API xx_mountainsort_mda *xx_mountainsort_mda_create(xx_io_device *,int64_t);
XXFC_API void xx_mountainsort_mda_destroy(xx_mountainsort_mda *);
XXFC_API void xx_mountainsort_mda_free(xx_mountainsort_mda *);
XXFC_API bool xx_mountainsort_mda_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mountainsort_mda_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
