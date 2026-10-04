/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only Apple MicroDrive partition map reader. No payload is executed.
 */
#ifndef XX_MICRODRIVE_H
#define XX_MICRODRIVE_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_microdrive;
typedef xx_microdrive xx_microdrive_t;
XXFC_API void xx_microdrive_init(xx_microdrive *,xx_io_device *,int64_t);
XXFC_API xx_microdrive *xx_microdrive_create(xx_io_device *,int64_t);
XXFC_API void xx_microdrive_destroy(xx_microdrive *);
XXFC_API void xx_microdrive_free(xx_microdrive *);
XXFC_API bool xx_microdrive_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_microdrive_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_microdrive_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_microdrive_to_format(xx_microdrive *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

