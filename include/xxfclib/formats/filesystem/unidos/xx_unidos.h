/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only UniDOS disk layout reader. No payload is executed.
 */
#ifndef XX_UNIDOS_H
#define XX_UNIDOS_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_unidos;
typedef xx_unidos xx_unidos_t;
XXFC_API void xx_unidos_init(xx_unidos *, xx_io_device *, int64_t);
XXFC_API xx_unidos *xx_unidos_create(xx_io_device *, int64_t);
XXFC_API void xx_unidos_destroy(xx_unidos *);
XXFC_API void xx_unidos_free(xx_unidos *);
XXFC_API bool xx_unidos_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_unidos_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_unidos_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
static inline Abstractformat *xx_unidos_to_format(xx_unidos *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
