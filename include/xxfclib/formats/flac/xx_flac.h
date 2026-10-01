/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.rfc-editor.org/rfc/rfc9639.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_FLAC_H
#define XX_FLAC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_flac { Abstractformat format; } xx_flac;
XXFC_API void xx_flac_init(xx_flac *,xx_io_device *,int64_t);
XXFC_API xx_flac *xx_flac_create(xx_io_device *,int64_t);
XXFC_API void xx_flac_destroy(xx_flac *);
XXFC_API void xx_flac_free(xx_flac *);
XXFC_API bool xx_flac_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_flac_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
