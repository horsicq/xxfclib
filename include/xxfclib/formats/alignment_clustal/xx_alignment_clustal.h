/* SPDX-License-Identifier: MIT
 * Wire specification: https://biopython.org/docs/latest/api/Bio.AlignIO.ClustalIO.html */
#ifndef XX_ALIGNMENT_CLUSTAL_H
#define XX_ALIGNMENT_CLUSTAL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_alignment_clustal { Abstractformat format; } xx_alignment_clustal;
XXFC_API void xx_alignment_clustal_init(xx_alignment_clustal *,xx_io_device *,int64_t);
XXFC_API xx_alignment_clustal *xx_alignment_clustal_create(xx_io_device *,int64_t);
XXFC_API void xx_alignment_clustal_destroy(xx_alignment_clustal *);
XXFC_API void xx_alignment_clustal_free(xx_alignment_clustal *);
XXFC_API bool xx_alignment_clustal_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_alignment_clustal_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_alignment_clustal_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_alignment_clustal_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_alignment_clustal_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_alignment_clustal_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_alignment_clustal_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
