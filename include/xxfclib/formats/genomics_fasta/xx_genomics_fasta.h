/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.ncbi.nlm.nih.gov/genbank/fastaformat/ */
#ifndef XX_GENOMICS_FASTA_H
#define XX_GENOMICS_FASTA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_fasta {
    Abstractformat format;
} xx_genomics_fasta;
XXFC_API void xx_genomics_fasta_init(xx_genomics_fasta *, xx_io_device *, int64_t);
XXFC_API xx_genomics_fasta *xx_genomics_fasta_create(xx_io_device *, int64_t);
XXFC_API void xx_genomics_fasta_destroy(xx_genomics_fasta *);
XXFC_API void xx_genomics_fasta_free(xx_genomics_fasta *);
XXFC_API bool xx_genomics_fasta_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_genomics_fasta_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_genomics_fasta_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_genomics_fasta_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_genomics_fasta_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_genomics_fasta_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_genomics_fasta_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
