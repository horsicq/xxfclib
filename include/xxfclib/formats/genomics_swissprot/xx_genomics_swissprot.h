/* SPDX-License-Identifier: MIT
 * Wire specification: https://web.expasy.org/docs/userman.html */
#ifndef XX_GENOMICS_SWISSPROT_H
#define XX_GENOMICS_SWISSPROT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_swissprot { Abstractformat format; } xx_genomics_swissprot;
XXFC_API void xx_genomics_swissprot_init(xx_genomics_swissprot *,xx_io_device *,int64_t);
XXFC_API xx_genomics_swissprot *xx_genomics_swissprot_create(xx_io_device *,int64_t);
XXFC_API void xx_genomics_swissprot_destroy(xx_genomics_swissprot *);
XXFC_API void xx_genomics_swissprot_free(xx_genomics_swissprot *);
XXFC_API bool xx_genomics_swissprot_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_genomics_swissprot_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_genomics_swissprot_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_genomics_swissprot_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_genomics_swissprot_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
