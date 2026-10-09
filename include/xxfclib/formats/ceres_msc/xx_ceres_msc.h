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
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ceres_msc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ceres_msc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ceres_msc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ceres_msc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ceres_msc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
