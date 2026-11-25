/* SPDX-License-Identifier: MIT */
#ifndef XX_TRACKER_REAL_H
#define XX_TRACKER_REAL_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_tracker_real {
    Abstractformat format;
} xx_tracker_real;
XXFC_API void xx_tracker_real_init(xx_tracker_real *, xx_io_device *, int64_t);
XXFC_API xx_tracker_real *xx_tracker_real_create(xx_io_device *, int64_t);
XXFC_API void xx_tracker_real_destroy(xx_tracker_real *);
XXFC_API void xx_tracker_real_free(xx_tracker_real *);
XXFC_API bool xx_tracker_real_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_tracker_real_handle_base_info(Abstractformat *, xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_real_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tracker_real_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_real_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_real_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tracker_real_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
