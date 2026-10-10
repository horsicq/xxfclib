/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_BGP_MESSAGES_H
#define XX_BGP_MESSAGES_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bgp_messages {
    Abstractformat format;
} xx_bgp_messages;
XXFC_API void xx_bgp_messages_init(xx_bgp_messages *, xx_io_device *, int64_t);
XXFC_API xx_bgp_messages *xx_bgp_messages_create(xx_io_device *, int64_t);
XXFC_API void xx_bgp_messages_destroy(xx_bgp_messages *);
XXFC_API void xx_bgp_messages_free(xx_bgp_messages *);
XXFC_API bool xx_bgp_messages_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_bgp_messages_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_bgp_messages_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_bgp_messages_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_bgp_messages_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_bgp_messages_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_bgp_messages_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
