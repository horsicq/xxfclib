/* SPDX-License-Identifier: MIT
 * Wire specification: https://biopython.org/docs/latest/api/Bio.SeqIO.SffIO.html */
#ifndef XX_GENOMICS_SFF_H
#define XX_GENOMICS_SFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_sff { Abstractformat format; } xx_genomics_sff;
XXFC_API void xx_genomics_sff_init(xx_genomics_sff *,xx_io_device *,int64_t);
XXFC_API xx_genomics_sff *xx_genomics_sff_create(xx_io_device *,int64_t);
XXFC_API void xx_genomics_sff_destroy(xx_genomics_sff *);
XXFC_API void xx_genomics_sff_free(xx_genomics_sff *);
XXFC_API bool xx_genomics_sff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_genomics_sff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_genomics_sff_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_genomics_sff_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_genomics_sff_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
