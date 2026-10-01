/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Norbyte/lslib/master/LSLib/LS/Resources/LSF/LSFCommon.cs
 * LSOF version2 stored string/node/attribute/value streams using metadata format0. Exports four encoded components; string chains, name references, node parents/attribute indices and value lengths checked. Compressed or extended versions and semantic resource reconstruction unsupported.
 */
#ifndef XX_LARIAN_LSF_H
#define XX_LARIAN_LSF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_larian_lsf { Abstractformat format; } xx_larian_lsf;
XXFC_API void xx_larian_lsf_init(xx_larian_lsf *,xx_io_device *,int64_t);
XXFC_API xx_larian_lsf *xx_larian_lsf_create(xx_io_device *,int64_t);
XXFC_API void xx_larian_lsf_destroy(xx_larian_lsf *);
XXFC_API void xx_larian_lsf_free(xx_larian_lsf *);
XXFC_API bool xx_larian_lsf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_larian_lsf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
