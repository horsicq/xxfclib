/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_DRAGON_VDK_H
#define XX_DRAGON_VDK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dragon_vdk { Abstractformat format; } xx_dragon_vdk;
XXFC_API void xx_dragon_vdk_init(xx_dragon_vdk *,xx_io_device *,int64_t);
XXFC_API xx_dragon_vdk *xx_dragon_vdk_create(xx_io_device *,int64_t);
XXFC_API void xx_dragon_vdk_destroy(xx_dragon_vdk *);
XXFC_API void xx_dragon_vdk_free(xx_dragon_vdk *);
XXFC_API bool xx_dragon_vdk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dragon_vdk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
