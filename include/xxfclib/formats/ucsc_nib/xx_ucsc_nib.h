/* SPDX-License-Identifier: MIT
 * Wire specification: https://genome.ucsc.edu/FAQ/FAQformat.html */
#ifndef XX_UCSC_NIB_H
#define XX_UCSC_NIB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ucsc_nib { Abstractformat format; } xx_ucsc_nib;
XXFC_API void xx_ucsc_nib_init(xx_ucsc_nib *,xx_io_device *,int64_t);
XXFC_API xx_ucsc_nib *xx_ucsc_nib_create(xx_io_device *,int64_t);
XXFC_API void xx_ucsc_nib_destroy(xx_ucsc_nib *);
XXFC_API void xx_ucsc_nib_free(xx_ucsc_nib *);
XXFC_API bool xx_ucsc_nib_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ucsc_nib_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
