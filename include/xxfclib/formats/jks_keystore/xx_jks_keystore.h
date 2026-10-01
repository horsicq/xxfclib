/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_JKS_KEYSTORE_H
#define XX_JKS_KEYSTORE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_jks_keystore {Abstractformat format;} xx_jks_keystore;
XXFC_API void xx_jks_keystore_init(xx_jks_keystore *,xx_io_device *,int64_t);
XXFC_API xx_jks_keystore *xx_jks_keystore_create(xx_io_device *,int64_t);
XXFC_API void xx_jks_keystore_destroy(xx_jks_keystore *);
XXFC_API void xx_jks_keystore_free(xx_jks_keystore *);
XXFC_API bool xx_jks_keystore_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_jks_keystore_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
