/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/OpenMW/openmw/master/components/bsa/ba2gnrlfile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_BETHESDA_BA2_H
#define XX_BETHESDA_BA2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bethesda_ba2 { Abstractformat format; } xx_bethesda_ba2;
XXFC_API void xx_bethesda_ba2_init(xx_bethesda_ba2 *,xx_io_device *,int64_t);
XXFC_API xx_bethesda_ba2 *xx_bethesda_ba2_create(xx_io_device *,int64_t);
XXFC_API void xx_bethesda_ba2_destroy(xx_bethesda_ba2 *);
XXFC_API void xx_bethesda_ba2_free(xx_bethesda_ba2 *);
XXFC_API bool xx_bethesda_ba2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bethesda_ba2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
