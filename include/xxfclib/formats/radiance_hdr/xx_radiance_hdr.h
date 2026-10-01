/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://floyd.lbl.gov/radiance/refer/filefmts.pdf, https://raw.githubusercontent.com/NREL/Radiance/master/src/common/color.c
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_RADIANCE_HDR_H
#define XX_RADIANCE_HDR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_radiance_hdr { Abstractformat format; } xx_radiance_hdr;
XXFC_API void xx_radiance_hdr_init(xx_radiance_hdr *,xx_io_device *,int64_t);
XXFC_API xx_radiance_hdr *xx_radiance_hdr_create(xx_io_device *,int64_t);
XXFC_API void xx_radiance_hdr_destroy(xx_radiance_hdr *);
XXFC_API void xx_radiance_hdr_free(xx_radiance_hdr *);
XXFC_API bool xx_radiance_hdr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_radiance_hdr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
