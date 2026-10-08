/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_ARP_PACKET_H
#define XX_ARP_PACKET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_arp_packet {Abstractformat format;} xx_arp_packet;
XXFC_API void xx_arp_packet_init(xx_arp_packet *,xx_io_device *,int64_t);
XXFC_API xx_arp_packet *xx_arp_packet_create(xx_io_device *,int64_t);
XXFC_API void xx_arp_packet_destroy(xx_arp_packet *);
XXFC_API void xx_arp_packet_free(xx_arp_packet *);
XXFC_API bool xx_arp_packet_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_arp_packet_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_arp_packet_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_arp_packet_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_arp_packet_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
