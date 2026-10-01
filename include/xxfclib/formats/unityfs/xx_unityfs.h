/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Perfare/AssetStudio/master/AssetStudio/BundleFile.cs
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_UNITYFS_H
#define XX_UNITYFS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_unityfs { Abstractformat format; } xx_unityfs;
XXFC_API void xx_unityfs_init(xx_unityfs *,xx_io_device *,int64_t);
XXFC_API xx_unityfs *xx_unityfs_create(xx_io_device *,int64_t);
XXFC_API void xx_unityfs_destroy(xx_unityfs *);
XXFC_API void xx_unityfs_free(xx_unityfs *);
XXFC_API bool xx_unityfs_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_unityfs_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
