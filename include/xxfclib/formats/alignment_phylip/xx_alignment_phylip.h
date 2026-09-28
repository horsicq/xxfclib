/* SPDX-License-Identifier: MIT
 * Wire specification: https://biopython.org/docs/latest/api/Bio.AlignIO.PhylipIO.html */
#ifndef XX_ALIGNMENT_PHYLIP_H
#define XX_ALIGNMENT_PHYLIP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_alignment_phylip { Abstractformat format; } xx_alignment_phylip;
XXFC_API void xx_alignment_phylip_init(xx_alignment_phylip *,xx_io_device *,int64_t);
XXFC_API xx_alignment_phylip *xx_alignment_phylip_create(xx_io_device *,int64_t);
XXFC_API void xx_alignment_phylip_destroy(xx_alignment_phylip *);
XXFC_API void xx_alignment_phylip_free(xx_alignment_phylip *);
XXFC_API bool xx_alignment_phylip_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_alignment_phylip_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
