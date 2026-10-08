/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_ERLANG_EXTERNAL_TERM_H
#define XX_ERLANG_EXTERNAL_TERM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_erlang_external_term { Abstractformat format; } xx_erlang_external_term;
XXFC_API void xx_erlang_external_term_init(xx_erlang_external_term *,xx_io_device *,int64_t);
XXFC_API xx_erlang_external_term *xx_erlang_external_term_create(xx_io_device *,int64_t);
XXFC_API void xx_erlang_external_term_destroy(xx_erlang_external_term *);
XXFC_API void xx_erlang_external_term_free(xx_erlang_external_term *);
XXFC_API bool xx_erlang_external_term_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_erlang_external_term_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_erlang_external_term_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_erlang_external_term_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_erlang_external_term_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
