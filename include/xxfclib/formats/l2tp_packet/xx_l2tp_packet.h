/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_L2TP_PACKET_H
#define XX_L2TP_PACKET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_l2tp_packet {Abstractformat format;} xx_l2tp_packet;
XXFC_API void xx_l2tp_packet_init(xx_l2tp_packet *,xx_io_device *,int64_t);
XXFC_API xx_l2tp_packet *xx_l2tp_packet_create(xx_io_device *,int64_t);
XXFC_API void xx_l2tp_packet_destroy(xx_l2tp_packet *);
XXFC_API void xx_l2tp_packet_free(xx_l2tp_packet *);
XXFC_API bool xx_l2tp_packet_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_l2tp_packet_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
