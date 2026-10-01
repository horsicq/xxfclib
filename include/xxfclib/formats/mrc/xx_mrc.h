/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.ccpem.ac.uk/mrc-format/mrc2014/ */
#ifndef XX_MRC_H
#define XX_MRC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mrc { Abstractformat format; } xx_mrc;
XXFC_API void xx_mrc_init(xx_mrc *,xx_io_device *,int64_t);
XXFC_API xx_mrc *xx_mrc_create(xx_io_device *,int64_t);
XXFC_API void xx_mrc_destroy(xx_mrc *);
XXFC_API void xx_mrc_free(xx_mrc *);
XXFC_API bool xx_mrc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mrc_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
