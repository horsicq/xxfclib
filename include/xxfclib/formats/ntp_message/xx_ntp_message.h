/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_NTP_MESSAGE_H
#define XX_NTP_MESSAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ntp_message {Abstractformat format;} xx_ntp_message;
XXFC_API void xx_ntp_message_init(xx_ntp_message *,xx_io_device *,int64_t);
XXFC_API xx_ntp_message *xx_ntp_message_create(xx_io_device *,int64_t);
XXFC_API void xx_ntp_message_destroy(xx_ntp_message *);
XXFC_API void xx_ntp_message_free(xx_ntp_message *);
XXFC_API bool xx_ntp_message_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ntp_message_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ntp_message_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ntp_message_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ntp_message_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
