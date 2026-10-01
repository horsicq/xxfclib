/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PCAP_H
#define XX_PCAP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pcap { Abstractformat format; } xx_pcap;
XXFC_API void xx_pcap_init(xx_pcap *,xx_io_device *,int64_t);
XXFC_API xx_pcap *xx_pcap_create(xx_io_device *,int64_t);
XXFC_API void xx_pcap_destroy(xx_pcap *);
XXFC_API void xx_pcap_free(xx_pcap *);
XXFC_API bool xx_pcap_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pcap_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
