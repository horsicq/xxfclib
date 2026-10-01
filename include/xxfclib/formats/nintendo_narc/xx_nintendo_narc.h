/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/RoadrunnerWMC/ndspy/master/ndspy/narc.py
 * Nintendo DS NARC v1 stored files. Uses stable numeric names; original filename directories are not reconstructed.
 */
#ifndef XX_NINTENDO_NARC_H
#define XX_NINTENDO_NARC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_narc { Abstractformat format; } xx_nintendo_narc;
XXFC_API void xx_nintendo_narc_init(xx_nintendo_narc *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_narc *xx_nintendo_narc_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_narc_destroy(xx_nintendo_narc *);
XXFC_API void xx_nintendo_narc_free(xx_nintendo_narc *);
XXFC_API bool xx_nintendo_narc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_narc_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
