/* SPDX-License-Identifier: MIT
 * Wire specification: https://developer.garmin.com/fit/protocol/ */
#ifndef XX_GARMIN_FIT_H
#define XX_GARMIN_FIT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_garmin_fit { Abstractformat format; } xx_garmin_fit;
XXFC_API void xx_garmin_fit_init(xx_garmin_fit *,xx_io_device *,int64_t);
XXFC_API xx_garmin_fit *xx_garmin_fit_create(xx_io_device *,int64_t);
XXFC_API void xx_garmin_fit_destroy(xx_garmin_fit *);
XXFC_API void xx_garmin_fit_free(xx_garmin_fit *);
XXFC_API bool xx_garmin_fit_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_garmin_fit_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_garmin_fit_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_garmin_fit_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_garmin_fit_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
