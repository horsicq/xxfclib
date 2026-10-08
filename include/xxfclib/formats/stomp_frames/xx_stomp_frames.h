/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_STOMP_FRAMES_H
#define XX_STOMP_FRAMES_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_stomp_frames {Abstractformat format;} xx_stomp_frames;
XXFC_API void xx_stomp_frames_init(xx_stomp_frames *,xx_io_device *,int64_t);
XXFC_API xx_stomp_frames *xx_stomp_frames_create(xx_io_device *,int64_t);
XXFC_API void xx_stomp_frames_destroy(xx_stomp_frames *);
XXFC_API void xx_stomp_frames_free(xx_stomp_frames *);
XXFC_API bool xx_stomp_frames_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_stomp_frames_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_stomp_frames_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_stomp_frames_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_stomp_frames_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
