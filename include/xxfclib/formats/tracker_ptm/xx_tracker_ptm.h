/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_TRACKER_PTM_H
#define XX_TRACKER_PTM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_ptm { Abstractformat format; } xx_tracker_ptm;
XXFC_API void xx_tracker_ptm_init(xx_tracker_ptm *,xx_io_device *,int64_t);
XXFC_API xx_tracker_ptm *xx_tracker_ptm_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_ptm_destroy(xx_tracker_ptm *);
XXFC_API void xx_tracker_ptm_free(xx_tracker_ptm *);
XXFC_API bool xx_tracker_ptm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_ptm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
