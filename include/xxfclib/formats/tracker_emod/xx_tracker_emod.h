/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_TRACKER_EMOD_H
#define XX_TRACKER_EMOD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tracker_emod { Abstractformat format; } xx_tracker_emod;
XXFC_API void xx_tracker_emod_init(xx_tracker_emod *,xx_io_device *,int64_t);
XXFC_API xx_tracker_emod *xx_tracker_emod_create(xx_io_device *,int64_t);
XXFC_API void xx_tracker_emod_destroy(xx_tracker_emod *);
XXFC_API void xx_tracker_emod_free(xx_tracker_emod *);
XXFC_API bool xx_tracker_emod_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tracker_emod_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tracker_emod_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tracker_emod_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tracker_emod_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tracker_emod_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tracker_emod_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
