/* SPDX-License-Identifier: MIT
 * Wire specification: https://samtools.github.io/hts-specs/VCFv4.3.pdf */
#ifndef XX_GENOMICS_VCF_H
#define XX_GENOMICS_VCF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_vcf { Abstractformat format; } xx_genomics_vcf;
XXFC_API void xx_genomics_vcf_init(xx_genomics_vcf *,xx_io_device *,int64_t);
XXFC_API xx_genomics_vcf *xx_genomics_vcf_create(xx_io_device *,int64_t);
XXFC_API void xx_genomics_vcf_destroy(xx_genomics_vcf *);
XXFC_API void xx_genomics_vcf_free(xx_genomics_vcf *);
XXFC_API bool xx_genomics_vcf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_genomics_vcf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
