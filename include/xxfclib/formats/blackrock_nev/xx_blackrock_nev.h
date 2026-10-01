/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/BlackrockNeurotech/NPMK/blob/master/NPMK/openNEV.m */
#ifndef XX_BLACKROCK_NEV_H
#define XX_BLACKROCK_NEV_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_blackrock_nev { Abstractformat format; } xx_blackrock_nev;
XXFC_API void xx_blackrock_nev_init(xx_blackrock_nev *,xx_io_device *,int64_t);
XXFC_API xx_blackrock_nev *xx_blackrock_nev_create(xx_io_device *,int64_t);
XXFC_API void xx_blackrock_nev_destroy(xx_blackrock_nev *);
XXFC_API void xx_blackrock_nev_free(xx_blackrock_nev *);
XXFC_API bool xx_blackrock_nev_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_blackrock_nev_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
