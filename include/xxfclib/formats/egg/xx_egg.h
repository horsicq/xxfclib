/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/dterracino/UnEgg/blob/master/EGG_Specification.pdf
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_EGG_H
#define XX_EGG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_egg { Abstractformat format; } xx_egg;
XXFC_API void xx_egg_init(xx_egg *,xx_io_device *,int64_t);
XXFC_API xx_egg *xx_egg_create(xx_io_device *,int64_t);
XXFC_API void xx_egg_destroy(xx_egg *);
XXFC_API void xx_egg_free(xx_egg *);
XXFC_API bool xx_egg_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_egg_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
