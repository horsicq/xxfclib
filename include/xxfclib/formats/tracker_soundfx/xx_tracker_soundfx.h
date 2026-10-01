/* SPDX-License-Identifier: MIT */
#ifndef XX_TRACKER_SOUNDFX_H
#define XX_TRACKER_SOUNDFX_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_tracker_soundfx {Abstractformat format;} xx_tracker_soundfx;
XXFC_API void xx_tracker_soundfx_init(xx_tracker_soundfx *,xx_io_device *,int64_t);
XXFC_API xx_tracker_soundfx *xx_tracker_soundfx_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_soundfx_destroy(xx_tracker_soundfx *);
XXFC_API void xx_tracker_soundfx_free(xx_tracker_soundfx *);
XXFC_API bool xx_tracker_soundfx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_soundfx_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
