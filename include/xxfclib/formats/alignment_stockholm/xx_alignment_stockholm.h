/* SPDX-License-Identifier: MIT
 * Wire specification: https://biopython.org/docs/latest/api/Bio.AlignIO.StockholmIO.html */
#ifndef XX_ALIGNMENT_STOCKHOLM_H
#define XX_ALIGNMENT_STOCKHOLM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_alignment_stockholm { Abstractformat format; } xx_alignment_stockholm;
XXFC_API void xx_alignment_stockholm_init(xx_alignment_stockholm *,xx_io_device *,int64_t);
XXFC_API xx_alignment_stockholm *xx_alignment_stockholm_create(xx_io_device *,int64_t);
XXFC_API void xx_alignment_stockholm_destroy(xx_alignment_stockholm *);
XXFC_API void xx_alignment_stockholm_free(xx_alignment_stockholm *);
XXFC_API bool xx_alignment_stockholm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_alignment_stockholm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
