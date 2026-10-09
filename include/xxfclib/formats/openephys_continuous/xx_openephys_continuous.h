/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/open-ephys/analysis-tools/blob/master/OpenEphys.py */
#ifndef XX_OPENEPHYS_CONTINUOUS_H
#define XX_OPENEPHYS_CONTINUOUS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openephys_continuous { Abstractformat format; } xx_openephys_continuous;
XXFC_API void xx_openephys_continuous_init(xx_openephys_continuous *,xx_io_device *,int64_t);
XXFC_API xx_openephys_continuous *xx_openephys_continuous_create(xx_io_device *,int64_t);
XXFC_API void xx_openephys_continuous_destroy(xx_openephys_continuous *);
XXFC_API void xx_openephys_continuous_free(xx_openephys_continuous *);
XXFC_API bool xx_openephys_continuous_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_openephys_continuous_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_openephys_continuous_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_openephys_continuous_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_openephys_continuous_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_openephys_continuous_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_openephys_continuous_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
