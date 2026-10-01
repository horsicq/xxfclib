/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.lmd.de/products/vcl/lmdtools/
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_LMD_CONTAINER_H
#define XX_LMD_CONTAINER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lmd_container { Abstractformat format; } xx_lmd_container;
XXFC_API void xx_lmd_container_init(xx_lmd_container *,xx_io_device *,int64_t);
XXFC_API xx_lmd_container *xx_lmd_container_create(xx_io_device *,int64_t);
XXFC_API void xx_lmd_container_destroy(xx_lmd_container *);
XXFC_API void xx_lmd_container_free(xx_lmd_container *);
XXFC_API bool xx_lmd_container_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lmd_container_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
