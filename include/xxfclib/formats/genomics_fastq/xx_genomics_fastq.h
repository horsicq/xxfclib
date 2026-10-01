/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.ncbi.nlm.nih.gov/sra/docs/submitformats/ */
#ifndef XX_GENOMICS_FASTQ_H
#define XX_GENOMICS_FASTQ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_fastq { Abstractformat format; } xx_genomics_fastq;
XXFC_API void xx_genomics_fastq_init(xx_genomics_fastq *,xx_io_device *,int64_t);
XXFC_API xx_genomics_fastq *xx_genomics_fastq_create(xx_io_device *,int64_t);
XXFC_API void xx_genomics_fastq_destroy(xx_genomics_fastq *);
XXFC_API void xx_genomics_fastq_free(xx_genomics_fastq *);
XXFC_API bool xx_genomics_fastq_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_genomics_fastq_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
