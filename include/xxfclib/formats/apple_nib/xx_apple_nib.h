/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Apple II nibble track image; exact extraction scope is documented in the source.
 */
#ifndef XX_APPLE_NIB_H
#define XX_APPLE_NIB_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_apple_nib;
XXFC_API void xx_apple_nib_init(xx_apple_nib *, xx_io_device *, int64_t);
XXFC_API xx_apple_nib *xx_apple_nib_create(xx_io_device *, int64_t);
XXFC_API void xx_apple_nib_destroy(xx_apple_nib *);
XXFC_API void xx_apple_nib_free(xx_apple_nib *);
XXFC_API bool xx_apple_nib_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_apple_nib_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_apple_nib_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
static inline Abstractformat *xx_apple_nib_to_format(xx_apple_nib *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
