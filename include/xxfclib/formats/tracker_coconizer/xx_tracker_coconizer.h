/* SPDX-License-Identifier: MIT */
#ifndef XX_TRACKER_COCONIZER_H
#define XX_TRACKER_COCONIZER_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_tracker_coconizer {Abstractformat format;} xx_tracker_coconizer;
XXFC_API void xx_tracker_coconizer_init(xx_tracker_coconizer *,xx_io_device *,int64_t);
XXFC_API xx_tracker_coconizer *xx_tracker_coconizer_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_coconizer_destroy(xx_tracker_coconizer *);
XXFC_API void xx_tracker_coconizer_free(xx_tracker_coconizer *);
XXFC_API bool xx_tracker_coconizer_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_coconizer_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
