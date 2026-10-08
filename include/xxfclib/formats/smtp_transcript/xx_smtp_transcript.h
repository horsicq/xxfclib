/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_SMTP_TRANSCRIPT_H
#define XX_SMTP_TRANSCRIPT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_smtp_transcript {Abstractformat format;} xx_smtp_transcript;
XXFC_API void xx_smtp_transcript_init(xx_smtp_transcript *,xx_io_device *,int64_t);
XXFC_API xx_smtp_transcript *xx_smtp_transcript_create(xx_io_device *,int64_t);
XXFC_API void xx_smtp_transcript_destroy(xx_smtp_transcript *);
XXFC_API void xx_smtp_transcript_free(xx_smtp_transcript *);
XXFC_API bool xx_smtp_transcript_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_smtp_transcript_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_smtp_transcript_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_smtp_transcript_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_smtp_transcript_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
