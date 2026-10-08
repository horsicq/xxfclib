/* SPDX-License-Identifier: MIT */
#ifndef XX_ADLIB_BAM_H
#define XX_ADLIB_BAM_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_adlib_bam {Abstractformat format;} xx_adlib_bam;
XXFC_API void xx_adlib_bam_init(xx_adlib_bam *,xx_io_device *,int64_t);
XXFC_API xx_adlib_bam *xx_adlib_bam_create(xx_io_device *,int64_t);
XXFC_API void xx_adlib_bam_destroy(xx_adlib_bam *);
XXFC_API void xx_adlib_bam_free(xx_adlib_bam *);
XXFC_API bool xx_adlib_bam_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adlib_bam_handle_base_info(Abstractformat *,xx_pd_struct *);
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adlib_bam_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adlib_bam_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adlib_bam_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
