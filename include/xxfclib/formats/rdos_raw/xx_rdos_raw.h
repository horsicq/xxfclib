/* SPDX-License-Identifier: MIT */
#ifndef XX_RDOS_RAW_H
#define XX_RDOS_RAW_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_rdos_raw {Abstractformat format;} xx_rdos_raw;
XXFC_API void xx_rdos_raw_init(xx_rdos_raw *,xx_io_device *,int64_t);
XXFC_API xx_rdos_raw *xx_rdos_raw_create(xx_io_device *,int64_t);
XXFC_API void xx_rdos_raw_destroy(xx_rdos_raw *);
XXFC_API void xx_rdos_raw_free(xx_rdos_raw *);
XXFC_API bool xx_rdos_raw_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_rdos_raw_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_rdos_raw_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_rdos_raw_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_rdos_raw_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_rdos_raw_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_rdos_raw_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
