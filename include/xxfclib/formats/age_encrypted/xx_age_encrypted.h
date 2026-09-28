/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_AGE_ENCRYPTED_H
#define XX_AGE_ENCRYPTED_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_age_encrypted {Abstractformat format;} xx_age_encrypted;
XXFC_API void xx_age_encrypted_init(xx_age_encrypted *,xx_io_device *,int64_t);
XXFC_API xx_age_encrypted *xx_age_encrypted_create(xx_io_device *,int64_t);
XXFC_API void xx_age_encrypted_destroy(xx_age_encrypted *);
XXFC_API void xx_age_encrypted_free(xx_age_encrypted *);
XXFC_API bool xx_age_encrypted_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_age_encrypted_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
