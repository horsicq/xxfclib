/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_RAD_H
#define XX_ADLIB_RAD_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_rad {Abstractformat format;} xx_adlib_rad;
XXFC_API void xx_adlib_rad_init(xx_adlib_rad *,xx_io_device *,int64_t);
XXFC_API xx_adlib_rad *xx_adlib_rad_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_rad_destroy(xx_adlib_rad *);
XXFC_API void xx_adlib_rad_free(xx_adlib_rad *);
XXFC_API bool xx_adlib_rad_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_rad_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adlib_rad_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_adlib_rad_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adlib_rad_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adlib_rad_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_adlib_rad_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
