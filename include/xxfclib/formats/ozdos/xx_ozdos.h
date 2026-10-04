/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only OzDOS disk layout reader. No payload is executed.
 */
#ifndef XX_OZDOS_H
#define XX_OZDOS_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_ozdos;
typedef xx_ozdos xx_ozdos_t;
XXFC_API void xx_ozdos_init(xx_ozdos *,xx_io_device *,int64_t);
XXFC_API xx_ozdos *xx_ozdos_create(xx_io_device *,int64_t);
XXFC_API void xx_ozdos_destroy(xx_ozdos *);
XXFC_API void xx_ozdos_free(xx_ozdos *);
XXFC_API bool xx_ozdos_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ozdos_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ozdos_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_ozdos_to_format(xx_ozdos *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

