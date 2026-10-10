/* SPDX-License-Identifier: MIT */
#ifndef XX_MAD_TRACKER_H
#define XX_MAD_TRACKER_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_mad_tracker {
    Abstractformat format;
} xx_mad_tracker;
XXFC_API void xx_mad_tracker_init(xx_mad_tracker *, xx_io_device *, int64_t);
XXFC_API xx_mad_tracker *xx_mad_tracker_create(xx_io_device *, int64_t);
XXFC_API void xx_mad_tracker_destroy(xx_mad_tracker *);
XXFC_API void xx_mad_tracker_free(xx_mad_tracker *);
XXFC_API bool xx_mad_tracker_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_mad_tracker_handle_base_info(Abstractformat *, xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mad_tracker_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mad_tracker_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mad_tracker_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mad_tracker_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mad_tracker_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
