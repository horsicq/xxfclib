/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.inivation.com/software/software-advanced-usage/file-formats/aedat-2.0.html */
#ifndef XX_INIVATION_AEDAT_H
#define XX_INIVATION_AEDAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_inivation_aedat { Abstractformat format; } xx_inivation_aedat;
XXFC_API void xx_inivation_aedat_init(xx_inivation_aedat *,xx_io_device *,int64_t);
XXFC_API xx_inivation_aedat *xx_inivation_aedat_create(xx_io_device *,int64_t);
XXFC_API void xx_inivation_aedat_destroy(xx_inivation_aedat *);
XXFC_API void xx_inivation_aedat_free(xx_inivation_aedat *);
XXFC_API bool xx_inivation_aedat_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_inivation_aedat_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
