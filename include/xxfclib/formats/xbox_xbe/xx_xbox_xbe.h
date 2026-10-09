/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/xemu-project/xemu/master/xemu-xbe.h
 * Original Xbox executable stored raw sections only; does not execute, relocate or verify executable signatures or section SHA-1 digests.
 */
#ifndef XX_XBOX_XBE_H
#define XX_XBOX_XBE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xbox_xbe { Abstractformat format; } xx_xbox_xbe;
XXFC_API void xx_xbox_xbe_init(xx_xbox_xbe *,xx_io_device *,int64_t);
XXFC_API xx_xbox_xbe *xx_xbox_xbe_create(xx_io_device *,int64_t);
XXFC_API void xx_xbox_xbe_destroy(xx_xbox_xbe *);
XXFC_API void xx_xbox_xbe_free(xx_xbox_xbe *);
XXFC_API bool xx_xbox_xbe_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_xbox_xbe_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_xbox_xbe_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_xbox_xbe_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_xbox_xbe_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_xbox_xbe_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_xbox_xbe_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
