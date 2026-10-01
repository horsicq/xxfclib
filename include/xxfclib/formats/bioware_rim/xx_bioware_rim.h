/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/xoreos/xoreos/master/src/aurora/rimfile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_BIOWARE_RIM_H
#define XX_BIOWARE_RIM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bioware_rim { Abstractformat format; } xx_bioware_rim;
XXFC_API void xx_bioware_rim_init(xx_bioware_rim *,xx_io_device *,int64_t);
XXFC_API xx_bioware_rim *xx_bioware_rim_create(xx_io_device *,int64_t);
XXFC_API void xx_bioware_rim_destroy(xx_bioware_rim *);
XXFC_API void xx_bioware_rim_free(xx_bioware_rim *);
XXFC_API bool xx_bioware_rim_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bioware_rim_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
