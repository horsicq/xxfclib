/* SPDX-License-Identifier: MIT */
#ifndef XX_TRACKER_MEGATRACKER_H
#define XX_TRACKER_MEGATRACKER_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_tracker_megatracker {Abstractformat format;} xx_tracker_megatracker;
XXFC_API void xx_tracker_megatracker_init(xx_tracker_megatracker *,xx_io_device *,int64_t);
XXFC_API xx_tracker_megatracker *xx_tracker_megatracker_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_megatracker_destroy(xx_tracker_megatracker *);
XXFC_API void xx_tracker_megatracker_free(xx_tracker_megatracker *);
XXFC_API bool xx_tracker_megatracker_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_megatracker_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
