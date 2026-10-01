/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/imageio/imageio/blob/master/imageio/plugins/spe.py */
#ifndef XX_PRINCETON_SPE_H
#define XX_PRINCETON_SPE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_princeton_spe { Abstractformat format; } xx_princeton_spe;
XXFC_API void xx_princeton_spe_init(xx_princeton_spe *,xx_io_device *,int64_t);
XXFC_API xx_princeton_spe *xx_princeton_spe_create(xx_io_device *,int64_t);
XXFC_API void xx_princeton_spe_destroy(xx_princeton_spe *);
XXFC_API void xx_princeton_spe_free(xx_princeton_spe *);
XXFC_API bool xx_princeton_spe_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_princeton_spe_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
