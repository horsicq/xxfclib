/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://github.com/libretro/libretro-handy/blob/master/lynx/cart.cpp
 * Publishes stored payload components; see docs/registered_second_fifty_formats.md.
 */
#ifndef XX_LYNX_LNX_H
#define XX_LYNX_LNX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lynx_lnx {
    Abstractformat format;
} xx_lynx_lnx;
XXFC_API void xx_lynx_lnx_init(xx_lynx_lnx *, xx_io_device *, int64_t);
XXFC_API xx_lynx_lnx *xx_lynx_lnx_create(xx_io_device *, int64_t);
XXFC_API void xx_lynx_lnx_destroy(xx_lynx_lnx *);
XXFC_API void xx_lynx_lnx_free(xx_lynx_lnx *);
XXFC_API bool xx_lynx_lnx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_lynx_lnx_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lynx_lnx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_lynx_lnx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lynx_lnx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lynx_lnx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_lynx_lnx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
