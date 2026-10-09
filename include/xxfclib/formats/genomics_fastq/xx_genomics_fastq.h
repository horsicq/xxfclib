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
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_genomics_fastq_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_genomics_fastq_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_genomics_fastq_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_genomics_fastq_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_genomics_fastq_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
