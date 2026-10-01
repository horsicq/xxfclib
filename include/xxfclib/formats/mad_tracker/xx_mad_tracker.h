/* SPDX-License-Identifier: MIT */
#ifndef XX_MAD_TRACKER_H
#define XX_MAD_TRACKER_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_mad_tracker {Abstractformat format;} xx_mad_tracker;
XXFC_API void xx_mad_tracker_init(xx_mad_tracker *,xx_io_device *,int64_t);
XXFC_API xx_mad_tracker *xx_mad_tracker_create(xx_io_device *,int64_t);
XXFC_API void xx_mad_tracker_destroy(xx_mad_tracker *);
XXFC_API void xx_mad_tracker_free(xx_mad_tracker *);
XXFC_API bool xx_mad_tracker_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mad_tracker_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
