/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_MAS_PACK_H
#define XXFCLIB_FORMAT_MAS_PACK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* MAS resource pack: a stored table-of-contents container (u32 count,
 * u32 data size, 32-byte entries of name[16]/offset/size) of a DOS game. */
XXFC_API Abstractformat *xx_mas_pack_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_mas_pack_free(Abstractformat *format);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mas_pack_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mas_pack_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mas_pack_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mas_pack_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mas_pack_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
