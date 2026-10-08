/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_NETFLOW_DATAGRAM_H
#define XX_NETFLOW_DATAGRAM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_netflow_datagram {Abstractformat format;} xx_netflow_datagram;
XXFC_API void xx_netflow_datagram_init(xx_netflow_datagram *,xx_io_device *,int64_t);
XXFC_API xx_netflow_datagram *xx_netflow_datagram_create(xx_io_device *,int64_t);
XXFC_API void xx_netflow_datagram_destroy(xx_netflow_datagram *);
XXFC_API void xx_netflow_datagram_free(xx_netflow_datagram *);
XXFC_API bool xx_netflow_datagram_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_netflow_datagram_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_netflow_datagram_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_netflow_datagram_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_netflow_datagram_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
