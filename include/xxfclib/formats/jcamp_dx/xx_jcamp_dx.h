/* SPDX-License-Identifier: MIT
 * Wire specification: https://iupac.org/what-we-do/digital-standards/jcamp-dx/ */
#ifndef XX_JCAMP_DX_H
#define XX_JCAMP_DX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_jcamp_dx { Abstractformat format; } xx_jcamp_dx;
XXFC_API void xx_jcamp_dx_init(xx_jcamp_dx *,xx_io_device *,int64_t);
XXFC_API xx_jcamp_dx *xx_jcamp_dx_create(xx_io_device *,int64_t);
XXFC_API void xx_jcamp_dx_destroy(xx_jcamp_dx *);
XXFC_API void xx_jcamp_dx_free(xx_jcamp_dx *);
XXFC_API bool xx_jcamp_dx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_jcamp_dx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_jcamp_dx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_jcamp_dx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_jcamp_dx_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
