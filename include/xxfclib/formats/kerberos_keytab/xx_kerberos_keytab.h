/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_KERBEROS_KEYTAB_H
#define XX_KERBEROS_KEYTAB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_kerberos_keytab { Abstractformat format; } xx_kerberos_keytab;
XXFC_API void xx_kerberos_keytab_init(xx_kerberos_keytab *,xx_io_device *,int64_t);
XXFC_API xx_kerberos_keytab *xx_kerberos_keytab_create(xx_io_device *,int64_t);
XXFC_API void xx_kerberos_keytab_destroy(xx_kerberos_keytab *);
XXFC_API void xx_kerberos_keytab_free(xx_kerberos_keytab *);
XXFC_API bool xx_kerberos_keytab_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_kerberos_keytab_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
