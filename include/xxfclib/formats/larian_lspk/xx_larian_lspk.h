/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/Norbyte/lslib/master/LSLib/LS/PackageReader.cs
 * LSPK version10 single-part archives with uncompressed file table and stored members. Checks optional CRC32. Solid, multi-part, compressed members and later LZ4-index package versions rejected.
 */
#ifndef XX_LARIAN_LSPK_H
#define XX_LARIAN_LSPK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_larian_lspk { Abstractformat format; } xx_larian_lspk;
XXFC_API void xx_larian_lspk_init(xx_larian_lspk *,xx_io_device *,int64_t);
XXFC_API xx_larian_lspk *xx_larian_lspk_create(xx_io_device *,int64_t);
XXFC_API void xx_larian_lspk_destroy(xx_larian_lspk *);
XXFC_API void xx_larian_lspk_free(xx_larian_lspk *);
XXFC_API bool xx_larian_lspk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_larian_lspk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
