/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/WizardMac/ReadStat/master/src/stata/readstat_dta_read.c
 * Bounded encoded-component extraction. */
#ifndef XX_STATA_DTA_H
#define XX_STATA_DTA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_stata_dta { Abstractformat format; } xx_stata_dta;
XXFC_API void xx_stata_dta_init(xx_stata_dta *,xx_io_device *,int64_t);
XXFC_API xx_stata_dta *xx_stata_dta_create(xx_io_device *,int64_t);
XXFC_API void xx_stata_dta_destroy(xx_stata_dta *);
XXFC_API void xx_stata_dta_free(xx_stata_dta *);
XXFC_API bool xx_stata_dta_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_stata_dta_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_stata_dta_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_stata_dta_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_stata_dta_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_stata_dta_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_stata_dta_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
