/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.rfc-editor.org/rfc/rfc9559.html, https://www.rfc-editor.org/rfc/rfc8794.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_MATROSKA_H
#define XX_MATROSKA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_matroska { Abstractformat format; } xx_matroska;
XXFC_API void xx_matroska_init(xx_matroska *,xx_io_device *,int64_t);
XXFC_API xx_matroska *xx_matroska_create(xx_io_device *,int64_t);
XXFC_API void xx_matroska_destroy(xx_matroska *);
XXFC_API void xx_matroska_free(xx_matroska *);
XXFC_API bool xx_matroska_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_matroska_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
