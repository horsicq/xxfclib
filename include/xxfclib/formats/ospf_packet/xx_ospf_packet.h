/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_OSPF_PACKET_H
#define XX_OSPF_PACKET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ospf_packet {Abstractformat format;} xx_ospf_packet;
XXFC_API void xx_ospf_packet_init(xx_ospf_packet *,xx_io_device *,int64_t);
XXFC_API xx_ospf_packet *xx_ospf_packet_create(xx_io_device *,int64_t);
XXFC_API void xx_ospf_packet_destroy(xx_ospf_packet *);
XXFC_API void xx_ospf_packet_free(xx_ospf_packet *);
XXFC_API bool xx_ospf_packet_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ospf_packet_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ospf_packet_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ospf_packet_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ospf_packet_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
