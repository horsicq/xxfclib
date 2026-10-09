/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_MQTT_PACKETS_H
#define XX_MQTT_PACKETS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mqtt_packets { Abstractformat format; } xx_mqtt_packets;
XXFC_API void xx_mqtt_packets_init(xx_mqtt_packets *,xx_io_device *,int64_t);
XXFC_API xx_mqtt_packets *xx_mqtt_packets_create(xx_io_device *,int64_t);
XXFC_API void xx_mqtt_packets_destroy(xx_mqtt_packets *);
XXFC_API void xx_mqtt_packets_free(xx_mqtt_packets *);
XXFC_API bool xx_mqtt_packets_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mqtt_packets_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mqtt_packets_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mqtt_packets_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mqtt_packets_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mqtt_packets_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mqtt_packets_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
