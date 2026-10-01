/* SPDX-License-Identifier: MIT */
#ifndef XX_TRACKER_ARCHIMEDES_H
#define XX_TRACKER_ARCHIMEDES_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_tracker_archimedes {Abstractformat format;} xx_tracker_archimedes;
XXFC_API void xx_tracker_archimedes_init(xx_tracker_archimedes *,xx_io_device *,int64_t);
XXFC_API xx_tracker_archimedes *xx_tracker_archimedes_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_archimedes_destroy(xx_tracker_archimedes *);
XXFC_API void xx_tracker_archimedes_free(xx_tracker_archimedes *);
XXFC_API bool xx_tracker_archimedes_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_archimedes_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
