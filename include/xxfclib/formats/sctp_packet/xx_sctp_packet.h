/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_SCTP_PACKET_H
#define XX_SCTP_PACKET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sctp_packet {Abstractformat format;} xx_sctp_packet;
XXFC_API void xx_sctp_packet_init(xx_sctp_packet *,xx_io_device *,int64_t);
XXFC_API xx_sctp_packet *xx_sctp_packet_create(xx_io_device *,int64_t);
XXFC_API void xx_sctp_packet_destroy(xx_sctp_packet *);
XXFC_API void xx_sctp_packet_free(xx_sctp_packet *);
XXFC_API bool xx_sctp_packet_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sctp_packet_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sctp_packet_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sctp_packet_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sctp_packet_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sctp_packet_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sctp_packet_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
