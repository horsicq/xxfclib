/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only Apple II cassette WAV reader. No payload is executed.
 */
#ifndef XX_APPLE_CASSETTE_H
#define XX_APPLE_CASSETTE_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_apple_cassette;
typedef xx_apple_cassette xx_apple_cassette_t;
XXFC_API void xx_apple_cassette_init(xx_apple_cassette *,xx_io_device *,int64_t);
XXFC_API xx_apple_cassette *xx_apple_cassette_create(xx_io_device *,int64_t);
XXFC_API void xx_apple_cassette_destroy(xx_apple_cassette *);
XXFC_API void xx_apple_cassette_free(xx_apple_cassette *);
XXFC_API bool xx_apple_cassette_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_cassette_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_cassette_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_apple_cassette_to_format(xx_apple_cassette *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

