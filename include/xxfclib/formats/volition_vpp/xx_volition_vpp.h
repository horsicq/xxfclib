/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/gibbed/Gibbed.Volition/blob/master/projects/Gibbed.Volition.FileFormats/PackageFileV3.cs
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_VOLITION_VPP_H
#define XX_VOLITION_VPP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_volition_vpp { Abstractformat format; } xx_volition_vpp;
XXFC_API void xx_volition_vpp_init(xx_volition_vpp *,xx_io_device *,int64_t);
XXFC_API xx_volition_vpp *xx_volition_vpp_create(xx_io_device *,int64_t);
XXFC_API void xx_volition_vpp_destroy(xx_volition_vpp *);
XXFC_API void xx_volition_vpp_free(xx_volition_vpp *);
XXFC_API bool xx_volition_vpp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_volition_vpp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
