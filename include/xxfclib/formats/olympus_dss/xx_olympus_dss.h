/* SPDX-License-Identifier: MIT. Bounded native olympus_dss reader. */
#ifndef XX_OLYMPUS_DSS_H
#define XX_OLYMPUS_DSS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_olympus_dss { Abstractformat format; } xx_olympus_dss;
XXFC_API void xx_olympus_dss_init(xx_olympus_dss *,xx_io_device *,int64_t);
XXFC_API xx_olympus_dss *xx_olympus_dss_create(xx_io_device *,int64_t);
XXFC_API void xx_olympus_dss_destroy(xx_olympus_dss *);
XXFC_API void xx_olympus_dss_free(xx_olympus_dss *);
XXFC_API bool xx_olympus_dss_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_olympus_dss_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_olympus_dss_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_olympus_dss_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_olympus_dss_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_olympus_dss_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_olympus_dss_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
