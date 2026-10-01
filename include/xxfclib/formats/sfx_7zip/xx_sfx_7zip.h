/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_SFX_7ZIP_H
#define XX_SFX_7ZIP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_7zip {
    Abstractformat format;
    void *nested_7zip; /* Validated archive at the embedded payload offset. */
} xx_sfx_7zip;
XXFC_API void xx_sfx_7zip_init(xx_sfx_7zip *,xx_io_device *,int64_t);
XXFC_API xx_sfx_7zip *xx_sfx_7zip_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_7zip_destroy(xx_sfx_7zip *);
XXFC_API void xx_sfx_7zip_free(xx_sfx_7zip *);
XXFC_API bool xx_sfx_7zip_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_7zip_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
