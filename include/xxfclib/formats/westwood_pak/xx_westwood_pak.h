/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Westwood PAK archive reader (Eye of the Beholder, Dune II, Kyrandia).
 * Format reference: https://github.com/Will40/dunepak/blob/master/src/main.rs
 */
#ifndef XX_WESTWOOD_PAK_H
#define XX_WESTWOOD_PAK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_westwood_pak { Abstractformat format; } xx_westwood_pak;
XXFC_API void xx_westwood_pak_init(xx_westwood_pak *,xx_io_device *,int64_t);
XXFC_API xx_westwood_pak *xx_westwood_pak_create(xx_io_device *,int64_t);
XXFC_API void xx_westwood_pak_destroy(xx_westwood_pak *);
XXFC_API void xx_westwood_pak_free(xx_westwood_pak *);
XXFC_API bool xx_westwood_pak_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_westwood_pak_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_westwood_pak_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_westwood_pak_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_westwood_pak_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
