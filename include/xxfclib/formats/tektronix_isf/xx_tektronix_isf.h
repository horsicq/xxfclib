/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.tek.com/en/support/faqs/what-format-isf-file */
#ifndef XX_TEKTRONIX_ISF_H
#define XX_TEKTRONIX_ISF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tektronix_isf { Abstractformat format; } xx_tektronix_isf;
XXFC_API void xx_tektronix_isf_init(xx_tektronix_isf *,xx_io_device *,int64_t);
XXFC_API xx_tektronix_isf *xx_tektronix_isf_create(xx_io_device *,int64_t);
XXFC_API void xx_tektronix_isf_destroy(xx_tektronix_isf *);
XXFC_API void xx_tektronix_isf_free(xx_tektronix_isf *);
XXFC_API bool xx_tektronix_isf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tektronix_isf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
