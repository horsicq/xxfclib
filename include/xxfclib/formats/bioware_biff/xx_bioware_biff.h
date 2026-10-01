/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/xoreos/xoreos/master/src/aurora/biffile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_BIOWARE_BIFF_H
#define XX_BIOWARE_BIFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bioware_biff { Abstractformat format; } xx_bioware_biff;
XXFC_API void xx_bioware_biff_init(xx_bioware_biff *,xx_io_device *,int64_t);
XXFC_API xx_bioware_biff *xx_bioware_biff_create(xx_io_device *,int64_t);
XXFC_API void xx_bioware_biff_destroy(xx_bioware_biff *);
XXFC_API void xx_bioware_biff_free(xx_bioware_biff *);
XXFC_API bool xx_bioware_biff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bioware_biff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
