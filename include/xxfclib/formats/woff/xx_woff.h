/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. WOFF 1.0, stored and zlib tables plus metadata/private data; verifies zlib and table checksums. No font rendering.
 */
#ifndef XX_WOFF_H
#define XX_WOFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_woff { Abstractformat format; } xx_woff;
XXFC_API void xx_woff_init(xx_woff *,xx_io_device *,int64_t);
XXFC_API xx_woff *xx_woff_create(xx_io_device *,int64_t);
XXFC_API void xx_woff_destroy(xx_woff *);
XXFC_API void xx_woff_free(xx_woff *);
XXFC_API bool xx_woff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_woff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
