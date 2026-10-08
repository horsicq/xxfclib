/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_TRACKER_AMS_H
#define XX_TRACKER_AMS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_ams { Abstractformat format; } xx_tracker_ams;
XXFC_API void xx_tracker_ams_init(xx_tracker_ams *,xx_io_device *,int64_t);
XXFC_API xx_tracker_ams *xx_tracker_ams_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_ams_destroy(xx_tracker_ams *);
XXFC_API void xx_tracker_ams_free(xx_tracker_ams *);
XXFC_API bool xx_tracker_ams_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_ams_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_ams_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_ams_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_ams_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
