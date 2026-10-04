/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only AppleLink PE reader. No payload is executed.
 */
#ifndef XX_APPLELINK_PE_H
#define XX_APPLELINK_PE_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_applelink_pe;
typedef xx_applelink_pe xx_applelink_pe_t;
XXFC_API void xx_applelink_pe_init(xx_applelink_pe *,xx_io_device *,int64_t);
XXFC_API xx_applelink_pe *xx_applelink_pe_create(xx_io_device *,int64_t);
XXFC_API void xx_applelink_pe_destroy(xx_applelink_pe *);
XXFC_API void xx_applelink_pe_free(xx_applelink_pe *);
XXFC_API bool xx_applelink_pe_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_applelink_pe_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_applelink_pe_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_applelink_pe_to_format(xx_applelink_pe *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

