/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/biopython/biopython/blob/master/Bio/Align/bigbed.py */
#ifndef XX_UCSC_BIGBED_H
#define XX_UCSC_BIGBED_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ucsc_bigbed { Abstractformat format; } xx_ucsc_bigbed;
XXFC_API void xx_ucsc_bigbed_init(xx_ucsc_bigbed *,xx_io_device *,int64_t);
XXFC_API xx_ucsc_bigbed *xx_ucsc_bigbed_create(xx_io_device *,int64_t);
XXFC_API void xx_ucsc_bigbed_destroy(xx_ucsc_bigbed *);
XXFC_API void xx_ucsc_bigbed_free(xx_ucsc_bigbed *);
XXFC_API bool xx_ucsc_bigbed_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ucsc_bigbed_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ucsc_bigbed_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ucsc_bigbed_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ucsc_bigbed_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ucsc_bigbed_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ucsc_bigbed_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
