/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/GNOME/gimp/master/app/core/gimppalette-load.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_ADOBE_ASE_H
#define XX_ADOBE_ASE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_adobe_ase { Abstractformat format; } xx_adobe_ase;
XXFC_API void xx_adobe_ase_init(xx_adobe_ase *,xx_io_device *,int64_t);
XXFC_API xx_adobe_ase *xx_adobe_ase_create(xx_io_device *,int64_t);
XXFC_API void xx_adobe_ase_destroy(xx_adobe_ase *);
XXFC_API void xx_adobe_ase_free(xx_adobe_ase *);
XXFC_API bool xx_adobe_ase_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adobe_ase_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adobe_ase_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adobe_ase_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adobe_ase_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
