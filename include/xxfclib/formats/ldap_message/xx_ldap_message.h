/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_LDAP_MESSAGE_H
#define XX_LDAP_MESSAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ldap_message {Abstractformat format;} xx_ldap_message;
XXFC_API void xx_ldap_message_init(xx_ldap_message *,xx_io_device *,int64_t);
XXFC_API xx_ldap_message *xx_ldap_message_create(xx_io_device *,int64_t);
XXFC_API void xx_ldap_message_destroy(xx_ldap_message *);
XXFC_API void xx_ldap_message_free(xx_ldap_message *);
XXFC_API bool xx_ldap_message_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ldap_message_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
