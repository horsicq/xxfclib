/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/BlackrockNeurotech/NPMK/blob/master/NPMK/openNSx.m */
#ifndef XX_BLACKROCK_NSX_H
#define XX_BLACKROCK_NSX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_blackrock_nsx { Abstractformat format; } xx_blackrock_nsx;
XXFC_API void xx_blackrock_nsx_init(xx_blackrock_nsx *,xx_io_device *,int64_t);
XXFC_API xx_blackrock_nsx *xx_blackrock_nsx_create(xx_io_device *,int64_t);
XXFC_API void xx_blackrock_nsx_destroy(xx_blackrock_nsx *);
XXFC_API void xx_blackrock_nsx_free(xx_blackrock_nsx *);
XXFC_API bool xx_blackrock_nsx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_blackrock_nsx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
