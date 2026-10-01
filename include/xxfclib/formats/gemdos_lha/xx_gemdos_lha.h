/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_GEMDOS_LHA_H
#define XX_GEMDOS_LHA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gemdos_lha { Abstractformat format; } xx_gemdos_lha;
XXFC_API void xx_gemdos_lha_init(xx_gemdos_lha *,xx_io_device *,int64_t);
XXFC_API xx_gemdos_lha *xx_gemdos_lha_create(xx_io_device *,int64_t);
XXFC_API void xx_gemdos_lha_destroy(xx_gemdos_lha *);
XXFC_API void xx_gemdos_lha_free(xx_gemdos_lha *);
XXFC_API bool xx_gemdos_lha_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gemdos_lha_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
