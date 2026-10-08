/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_ETHERNET_FRAME_H
#define XX_ETHERNET_FRAME_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ethernet_frame {Abstractformat format;} xx_ethernet_frame;
XXFC_API void xx_ethernet_frame_init(xx_ethernet_frame *,xx_io_device *,int64_t);
XXFC_API xx_ethernet_frame *xx_ethernet_frame_create(xx_io_device *,int64_t);
XXFC_API void xx_ethernet_frame_destroy(xx_ethernet_frame *);
XXFC_API void xx_ethernet_frame_free(xx_ethernet_frame *);
XXFC_API bool xx_ethernet_frame_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ethernet_frame_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ethernet_frame_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ethernet_frame_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ethernet_frame_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
