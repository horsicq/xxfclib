/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_ADOBE_ACO_H
#define XX_ADOBE_ACO_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_adobe_aco { Abstractformat format; } xx_adobe_aco;
XXFC_API void xx_adobe_aco_init(xx_adobe_aco *,xx_io_device *,int64_t);
XXFC_API xx_adobe_aco *xx_adobe_aco_create(xx_io_device *,int64_t);
XXFC_API void xx_adobe_aco_destroy(xx_adobe_aco *);
XXFC_API void xx_adobe_aco_free(xx_adobe_aco *);
XXFC_API bool xx_adobe_aco_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adobe_aco_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
