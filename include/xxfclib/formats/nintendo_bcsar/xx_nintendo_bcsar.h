/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://gota7.github.io/Citric-Composer/specs/common.html
 * NintendoWare endian-aware bounded section extraction only. Exports complete encoded STRG/INFO/FILE blocks; individual nested sounds/external files are not resolved.
 */
#ifndef XX_NINTENDO_BCSAR_H
#define XX_NINTENDO_BCSAR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bcsar { Abstractformat format; } xx_nintendo_bcsar;
XXFC_API void xx_nintendo_bcsar_init(xx_nintendo_bcsar *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bcsar *xx_nintendo_bcsar_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bcsar_destroy(xx_nintendo_bcsar *);
XXFC_API void xx_nintendo_bcsar_free(xx_nintendo_bcsar *);
XXFC_API bool xx_nintendo_bcsar_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bcsar_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
