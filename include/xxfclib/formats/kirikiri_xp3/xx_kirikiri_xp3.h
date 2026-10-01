/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/krkrz/krkrz/blob/master/base/XP3Archive.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_KIRIKIRI_XP3_H
#define XX_KIRIKIRI_XP3_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_kirikiri_xp3 { Abstractformat format; } xx_kirikiri_xp3;
XXFC_API void xx_kirikiri_xp3_init(xx_kirikiri_xp3 *,xx_io_device *,int64_t);
XXFC_API xx_kirikiri_xp3 *xx_kirikiri_xp3_create(xx_io_device *,int64_t);
XXFC_API void xx_kirikiri_xp3_destroy(xx_kirikiri_xp3 *);
XXFC_API void xx_kirikiri_xp3_free(xx_kirikiri_xp3 *);
XXFC_API bool xx_kirikiri_xp3_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_kirikiri_xp3_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
