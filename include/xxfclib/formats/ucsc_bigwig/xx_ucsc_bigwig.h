/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/dpryan79/libBigWig */
#ifndef XX_UCSC_BIGWIG_H
#define XX_UCSC_BIGWIG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ucsc_bigwig { Abstractformat format; } xx_ucsc_bigwig;
XXFC_API void xx_ucsc_bigwig_init(xx_ucsc_bigwig *,xx_io_device *,int64_t);
XXFC_API xx_ucsc_bigwig *xx_ucsc_bigwig_create(xx_io_device *,int64_t);
XXFC_API void xx_ucsc_bigwig_destroy(xx_ucsc_bigwig *);
XXFC_API void xx_ucsc_bigwig_free(xx_ucsc_bigwig *);
XXFC_API bool xx_ucsc_bigwig_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ucsc_bigwig_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ucsc_bigwig_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ucsc_bigwig_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ucsc_bigwig_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ucsc_bigwig_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ucsc_bigwig_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
