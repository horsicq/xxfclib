/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://planetside.co.uk/wiki/index.php?title=Terragen_.TER_Format
 * Terragen classic TER: unique SIZE/XPTS/YPTS geometry, positive finite SCAL/CRAD and fixed CRVM chunks, complete ALTW signed16 heightfield and terminal EOF. Unknown chunks and terrain rendering unsupported. Original heightfield and descriptors exported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_TERRAGEN_TER_H
#define XX_TERRAGEN_TER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_terragen_ter {Abstractformat format;} xx_terragen_ter;
XXFC_API void xx_terragen_ter_init(xx_terragen_ter *,xx_io_device *,int64_t);
XXFC_API xx_terragen_ter *xx_terragen_ter_create(xx_io_device *,int64_t);
XXFC_API void xx_terragen_ter_destroy(xx_terragen_ter *);
XXFC_API void xx_terragen_ter_free(xx_terragen_ter *);
XXFC_API bool xx_terragen_ter_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_terragen_ter_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
