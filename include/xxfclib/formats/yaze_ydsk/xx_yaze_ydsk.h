/* SPDX-License-Identifier: MIT */
#ifndef XX_YAZE_YDSK_H
#define XX_YAZE_YDSK_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_yaze_ydsk {Abstractformat format;} xx_yaze_ydsk;
XXFC_API void xx_yaze_ydsk_init(xx_yaze_ydsk *,xx_io_device *,int64_t);
XXFC_API xx_yaze_ydsk *xx_yaze_ydsk_create(xx_io_device *,int64_t);
XXFC_API void xx_yaze_ydsk_destroy(xx_yaze_ydsk *);
XXFC_API void xx_yaze_ydsk_free(xx_yaze_ydsk *);
XXFC_API bool xx_yaze_ydsk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_yaze_ydsk_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
