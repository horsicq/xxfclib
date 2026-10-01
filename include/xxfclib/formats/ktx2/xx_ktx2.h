/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html
 * KTX2 non-supercompressed descriptor, metadata and encoded mip levels. BasisLZ/Zstandard/zlib supercompression is rejected; no pixel decoding.
 */
#ifndef XX_KTX2_H
#define XX_KTX2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ktx2 { Abstractformat format; } xx_ktx2;
XXFC_API void xx_ktx2_init(xx_ktx2 *,xx_io_device *,int64_t);
XXFC_API xx_ktx2 *xx_ktx2_create(xx_io_device *,int64_t);
XXFC_API void xx_ktx2_destroy(xx_ktx2 *);
XXFC_API void xx_ktx2_free(xx_ktx2 *);
XXFC_API bool xx_ktx2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ktx2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
