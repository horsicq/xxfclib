/* SPDX-License-Identifier: MIT */
#ifndef XX_TRACKER_DTM_H
#define XX_TRACKER_DTM_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_tracker_dtm {Abstractformat format;} xx_tracker_dtm;
XXFC_API void xx_tracker_dtm_init(xx_tracker_dtm *,xx_io_device *,int64_t);
XXFC_API xx_tracker_dtm *xx_tracker_dtm_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_dtm_destroy(xx_tracker_dtm *);
XXFC_API void xx_tracker_dtm_free(xx_tracker_dtm *);
XXFC_API bool xx_tracker_dtm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_dtm_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
