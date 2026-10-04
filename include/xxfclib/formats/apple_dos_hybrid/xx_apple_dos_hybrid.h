/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only Apple DOS hybrid disk reader. No payload is executed.
 */
#ifndef XX_APPLE_DOS_HYBRID_H
#define XX_APPLE_DOS_HYBRID_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_apple_dos_hybrid;
typedef xx_apple_dos_hybrid xx_apple_dos_hybrid_t;
XXFC_API void xx_apple_dos_hybrid_init(xx_apple_dos_hybrid *,xx_io_device *,int64_t);
XXFC_API xx_apple_dos_hybrid *xx_apple_dos_hybrid_create(xx_io_device *,int64_t);
XXFC_API void xx_apple_dos_hybrid_destroy(xx_apple_dos_hybrid *);
XXFC_API void xx_apple_dos_hybrid_free(xx_apple_dos_hybrid *);
XXFC_API bool xx_apple_dos_hybrid_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_dos_hybrid_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_dos_hybrid_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_apple_dos_hybrid_to_format(xx_apple_dos_hybrid *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

