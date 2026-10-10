/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/The-Sequence-Ontology/Specifications/master/gff3.md */
#ifndef XX_GENOMICS_GFF3_H
#define XX_GENOMICS_GFF3_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_genomics_gff3 {
    Abstractformat format;
} xx_genomics_gff3;
XXFC_API void xx_genomics_gff3_init(xx_genomics_gff3 *, xx_io_device *, int64_t);
XXFC_API xx_genomics_gff3 *xx_genomics_gff3_create(xx_io_device *, int64_t);
XXFC_API void xx_genomics_gff3_destroy(xx_genomics_gff3 *);
XXFC_API void xx_genomics_gff3_free(xx_genomics_gff3 *);
XXFC_API bool xx_genomics_gff3_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_genomics_gff3_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_genomics_gff3_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_genomics_gff3_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_genomics_gff3_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_genomics_gff3_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_genomics_gff3_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
