/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_MODBUS_TCP_H
#define XX_MODBUS_TCP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_modbus_tcp {Abstractformat format;} xx_modbus_tcp;
XXFC_API void xx_modbus_tcp_init(xx_modbus_tcp *,xx_io_device *,int64_t);
XXFC_API xx_modbus_tcp *xx_modbus_tcp_create(xx_io_device *,int64_t);
XXFC_API void xx_modbus_tcp_destroy(xx_modbus_tcp *);
XXFC_API void xx_modbus_tcp_free(xx_modbus_tcp *);
XXFC_API bool xx_modbus_tcp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_modbus_tcp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_modbus_tcp_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_modbus_tcp_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_modbus_tcp_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
