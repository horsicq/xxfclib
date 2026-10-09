/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_LLDP_MESSAGE_H
#define XX_LLDP_MESSAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lldp_message {Abstractformat format;} xx_lldp_message;
XXFC_API void xx_lldp_message_init(xx_lldp_message *,xx_io_device *,int64_t);
XXFC_API xx_lldp_message *xx_lldp_message_create(xx_io_device *,int64_t);
XXFC_API void xx_lldp_message_destroy(xx_lldp_message *);
XXFC_API void xx_lldp_message_free(xx_lldp_message *);
XXFC_API bool xx_lldp_message_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lldp_message_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lldp_message_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_lldp_message_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lldp_message_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lldp_message_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_lldp_message_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
