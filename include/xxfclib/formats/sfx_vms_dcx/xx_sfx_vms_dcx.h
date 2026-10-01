/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * OpenVMS FTSV DCX self-extracting image.
 */
#ifndef XX_SFX_VMS_DCX_H
#define XX_SFX_VMS_DCX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_vms_dcx { Abstractformat format; } xx_sfx_vms_dcx;
XXFC_API void xx_sfx_vms_dcx_init(xx_sfx_vms_dcx *, xx_io_device *, int64_t);
XXFC_API xx_sfx_vms_dcx *xx_sfx_vms_dcx_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_vms_dcx_destroy(xx_sfx_vms_dcx *);
XXFC_API void xx_sfx_vms_dcx_free(xx_sfx_vms_dcx *);
XXFC_API bool xx_sfx_vms_dcx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_vms_dcx_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
