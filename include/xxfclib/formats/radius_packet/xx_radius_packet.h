/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_RADIUS_PACKET_H
#define XX_RADIUS_PACKET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_radius_packet {Abstractformat format;} xx_radius_packet;
XXFC_API void xx_radius_packet_init(xx_radius_packet *,xx_io_device *,int64_t);
XXFC_API xx_radius_packet *xx_radius_packet_create(xx_io_device *,int64_t);
XXFC_API void xx_radius_packet_destroy(xx_radius_packet *);
XXFC_API void xx_radius_packet_free(xx_radius_packet *);
XXFC_API bool xx_radius_packet_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_radius_packet_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
