/* SPDX-License-Identifier: MIT
 * Wire specification: https://genome.ucsc.edu/goldenPath/help/wiggle.html */
#ifndef XX_GENOMICS_WIGGLE_H
#define XX_GENOMICS_WIGGLE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_wiggle { Abstractformat format; } xx_genomics_wiggle;
XXFC_API void xx_genomics_wiggle_init(xx_genomics_wiggle *,xx_io_device *,int64_t);
XXFC_API xx_genomics_wiggle *xx_genomics_wiggle_create(xx_io_device *,int64_t);
XXFC_API void xx_genomics_wiggle_destroy(xx_genomics_wiggle *);
XXFC_API void xx_genomics_wiggle_free(xx_genomics_wiggle *);
XXFC_API bool xx_genomics_wiggle_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_genomics_wiggle_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_genomics_wiggle_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_genomics_wiggle_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_genomics_wiggle_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_genomics_wiggle_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_genomics_wiggle_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
