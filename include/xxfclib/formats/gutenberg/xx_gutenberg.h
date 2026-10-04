/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only Gutenberg filesystem reader. No payload is executed.
 */
#ifndef XX_GUTENBERG_H
#define XX_GUTENBERG_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_gutenberg;
typedef xx_gutenberg xx_gutenberg_t;
XXFC_API void xx_gutenberg_init(xx_gutenberg *,xx_io_device *,int64_t);
XXFC_API xx_gutenberg *xx_gutenberg_create(xx_io_device *,int64_t);
XXFC_API void xx_gutenberg_destroy(xx_gutenberg *);
XXFC_API void xx_gutenberg_free(xx_gutenberg *);
XXFC_API bool xx_gutenberg_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gutenberg_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gutenberg_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_gutenberg_to_format(xx_gutenberg *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

