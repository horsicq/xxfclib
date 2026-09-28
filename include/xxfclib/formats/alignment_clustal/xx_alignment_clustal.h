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
#endif
