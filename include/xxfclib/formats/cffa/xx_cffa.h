/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only CFFA disk layout reader. No payload is executed.
 */
#ifndef XX_CFFA_H
#define XX_CFFA_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_cffa;
typedef xx_cffa xx_cffa_t;
XXFC_API void xx_cffa_init(xx_cffa *,xx_io_device *,int64_t);
XXFC_API xx_cffa *xx_cffa_create(xx_io_device *,int64_t);
XXFC_API void xx_cffa_destroy(xx_cffa *);
XXFC_API void xx_cffa_free(xx_cffa *);
XXFC_API bool xx_cffa_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cffa_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cffa_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_cffa_to_format(xx_cffa *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

