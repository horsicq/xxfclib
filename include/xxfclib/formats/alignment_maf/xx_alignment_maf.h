/* SPDX-License-Identifier: MIT
 * Wire specification: https://genome.ucsc.edu/FAQ/FAQformat.html#format5 */
#ifndef XX_ALIGNMENT_MAF_H
#define XX_ALIGNMENT_MAF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_alignment_maf { Abstractformat format; } xx_alignment_maf;
XXFC_API void xx_alignment_maf_init(xx_alignment_maf *,xx_io_device *,int64_t);
XXFC_API xx_alignment_maf *xx_alignment_maf_create(xx_io_device *,int64_t);
XXFC_API void xx_alignment_maf_destroy(xx_alignment_maf *);
XXFC_API void xx_alignment_maf_free(xx_alignment_maf *);
XXFC_API bool xx_alignment_maf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_alignment_maf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
