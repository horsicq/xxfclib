/* SPDX-License-Identifier: MIT */
#ifndef XX_TRACKER_REAL_H
#define XX_TRACKER_REAL_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_tracker_real {Abstractformat format;} xx_tracker_real;
XXFC_API void xx_tracker_real_init(xx_tracker_real *,xx_io_device *,int64_t);
XXFC_API xx_tracker_real *xx_tracker_real_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_real_destroy(xx_tracker_real *);
XXFC_API void xx_tracker_real_free(xx_tracker_real *);
XXFC_API bool xx_tracker_real_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_real_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
