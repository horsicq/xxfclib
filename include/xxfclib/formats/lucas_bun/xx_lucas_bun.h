/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/scummvm/scummvm/master/engines/scumm/imuse_digi/dimuse_bndmgr.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_LUCAS_BUN_H
#define XX_LUCAS_BUN_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lucas_bun { Abstractformat format; } xx_lucas_bun;
XXFC_API void xx_lucas_bun_init(xx_lucas_bun *,xx_io_device *,int64_t);
XXFC_API xx_lucas_bun *xx_lucas_bun_create(xx_io_device *,int64_t);
XXFC_API void xx_lucas_bun_destroy(xx_lucas_bun *);
XXFC_API void xx_lucas_bun_free(xx_lucas_bun *);
XXFC_API bool xx_lucas_bun_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lucas_bun_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lucas_bun_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lucas_bun_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lucas_bun_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
