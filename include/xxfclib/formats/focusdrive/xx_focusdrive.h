/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only FocusDrive partition map reader. No payload is executed.
 */
#ifndef XX_FOCUSDRIVE_H
#define XX_FOCUSDRIVE_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_focusdrive;
typedef xx_focusdrive xx_focusdrive_t;
XXFC_API void xx_focusdrive_init(xx_focusdrive *,xx_io_device *,int64_t);
XXFC_API xx_focusdrive *xx_focusdrive_create(xx_io_device *,int64_t);
XXFC_API void xx_focusdrive_destroy(xx_focusdrive *);
XXFC_API void xx_focusdrive_free(xx_focusdrive *);
XXFC_API bool xx_focusdrive_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_focusdrive_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_focusdrive_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_focusdrive_to_format(xx_focusdrive *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

