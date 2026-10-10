/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only SSI Apple RDOS reader. No payload is executed.
 */
#ifndef XX_APPLE_RDOS_H
#define XX_APPLE_RDOS_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_apple_rdos;
typedef xx_apple_rdos xx_apple_rdos_t;
XXFC_API void xx_apple_rdos_init(xx_apple_rdos *, xx_io_device *, int64_t);
XXFC_API xx_apple_rdos *xx_apple_rdos_create(xx_io_device *, int64_t);
XXFC_API void xx_apple_rdos_destroy(xx_apple_rdos *);
XXFC_API void xx_apple_rdos_free(xx_apple_rdos *);
XXFC_API bool xx_apple_rdos_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_apple_rdos_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_apple_rdos_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
static inline Abstractformat *xx_apple_rdos_to_format(xx_apple_rdos *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
