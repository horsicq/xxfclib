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
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_yaze_ydsk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_yaze_ydsk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_yaze_ydsk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_yaze_ydsk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_yaze_ydsk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
