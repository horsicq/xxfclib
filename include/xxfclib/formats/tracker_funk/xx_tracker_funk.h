/* SPDX-License-Identifier: MIT */
#ifndef XX_TRACKER_FUNK_H
#define XX_TRACKER_FUNK_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_tracker_funk {Abstractformat format;} xx_tracker_funk;
XXFC_API void xx_tracker_funk_init(xx_tracker_funk *,xx_io_device *,int64_t);
XXFC_API xx_tracker_funk *xx_tracker_funk_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_funk_destroy(xx_tracker_funk *);
XXFC_API void xx_tracker_funk_free(xx_tracker_funk *);
XXFC_API bool xx_tracker_funk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_funk_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
