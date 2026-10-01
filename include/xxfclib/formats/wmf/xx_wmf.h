/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://download.microsoft.com/download/0/B/E/0BE8BDD7-E5E8-422A-ABFD-4342ED7AD886/WindowsMetafileFormat%28wmf%29Specification.pdf
 * Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_WMF_H
#define XX_WMF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wmf { Abstractformat format; } xx_wmf;
XXFC_API void xx_wmf_init(xx_wmf *,xx_io_device *,int64_t);
XXFC_API xx_wmf *xx_wmf_create(xx_io_device *,int64_t);
XXFC_API void xx_wmf_destroy(xx_wmf *);
XXFC_API void xx_wmf_free(xx_wmf *);
XXFC_API bool xx_wmf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_wmf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
