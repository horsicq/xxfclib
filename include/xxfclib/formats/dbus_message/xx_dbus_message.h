/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_DBUS_MESSAGE_H
#define XX_DBUS_MESSAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dbus_message { Abstractformat format; } xx_dbus_message;
XXFC_API void xx_dbus_message_init(xx_dbus_message *,xx_io_device *,int64_t);
XXFC_API xx_dbus_message *xx_dbus_message_create(xx_io_device *,int64_t);
XXFC_API void xx_dbus_message_destroy(xx_dbus_message *);
XXFC_API void xx_dbus_message_free(xx_dbus_message *);
XXFC_API bool xx_dbus_message_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dbus_message_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
