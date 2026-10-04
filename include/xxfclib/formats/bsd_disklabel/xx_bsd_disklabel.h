/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only BSD disklabel reader. No payload is executed.
 */
#ifndef XX_BSD_DISKLABEL_H
#define XX_BSD_DISKLABEL_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_bsd_disklabel;
typedef xx_bsd_disklabel xx_bsd_disklabel_t;
XXFC_API void xx_bsd_disklabel_init(xx_bsd_disklabel *,xx_io_device *,int64_t);
XXFC_API xx_bsd_disklabel *xx_bsd_disklabel_create(xx_io_device *,int64_t);
XXFC_API void xx_bsd_disklabel_destroy(xx_bsd_disklabel *);
XXFC_API void xx_bsd_disklabel_free(xx_bsd_disklabel *);
XXFC_API bool xx_bsd_disklabel_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bsd_disklabel_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bsd_disklabel_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_bsd_disklabel_to_format(xx_bsd_disklabel *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

