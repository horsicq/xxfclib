/* SPDX-License-Identifier: MIT */
#ifndef XX_CERES_MSC_H
#define XX_CERES_MSC_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_ceres_msc {Abstractformat format;} xx_ceres_msc;
XXFC_API void xx_ceres_msc_init(xx_ceres_msc *,xx_io_device *,int64_t);
XXFC_API xx_ceres_msc *xx_ceres_msc_create(xx_io_device *,int64_t);
XXFC_API void xx_ceres_msc_destroy(xx_ceres_msc *);
XXFC_API void xx_ceres_msc_free(xx_ceres_msc *);
XXFC_API bool xx_ceres_msc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ceres_msc_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
