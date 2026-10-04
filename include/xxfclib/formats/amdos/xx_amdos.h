/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only AmDOS disk layout reader. No payload is executed.
 */
#ifndef XX_AMDOS_H
#define XX_AMDOS_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_amdos;
typedef xx_amdos xx_amdos_t;
XXFC_API void xx_amdos_init(xx_amdos *,xx_io_device *,int64_t);
XXFC_API xx_amdos *xx_amdos_create(xx_io_device *,int64_t);
XXFC_API void xx_amdos_destroy(xx_amdos *);
XXFC_API void xx_amdos_free(xx_amdos *);
XXFC_API bool xx_amdos_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amdos_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amdos_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_amdos_to_format(xx_amdos *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

