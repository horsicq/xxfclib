/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_SNMP_MESSAGE_H
#define XX_SNMP_MESSAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_snmp_message {Abstractformat format;} xx_snmp_message;
XXFC_API void xx_snmp_message_init(xx_snmp_message *,xx_io_device *,int64_t);
XXFC_API xx_snmp_message *xx_snmp_message_create(xx_io_device *,int64_t);
XXFC_API void xx_snmp_message_destroy(xx_snmp_message *);
XXFC_API void xx_snmp_message_free(xx_snmp_message *);
XXFC_API bool xx_snmp_message_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_snmp_message_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_snmp_message_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_snmp_message_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_snmp_message_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
