/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_SOMEIP_MESSAGE_H
#define XX_SOMEIP_MESSAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_someip_message {Abstractformat format;} xx_someip_message;
XXFC_API void xx_someip_message_init(xx_someip_message *,xx_io_device *,int64_t);
XXFC_API xx_someip_message *xx_someip_message_create(xx_io_device *,int64_t);
XXFC_API void xx_someip_message_destroy(xx_someip_message *);
XXFC_API void xx_someip_message_free(xx_someip_message *);
XXFC_API bool xx_someip_message_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_someip_message_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
