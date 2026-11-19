/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_IMPRESSIONS_RES_H
#define XXFCLIB_FORMAT_IMPRESSIONS_RES_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Impressions ".RES" resource archives (extension .gob; Lords of the Realm and
 * other Impressions/Sierra titles of that engine). A ".RES" magic, a dword
 * pointer to the member table at the file tail, then count * 52-byte entries
 * (36-byte name, flag, packed size, original size, absolute offset). Members
 * are stored (packed == original) or packed with the engine's LZ codec
 * (chunked LZSS+RLE, or a headerless 9..14-bit LZW for a few members). */
XXFC_API Abstractformat *xx_impressions_res_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_impressions_res_free(Abstractformat *format);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_impressions_res_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_impressions_res_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_impressions_res_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_impressions_res_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_impressions_res_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_IMPRESSIONS_RES_H */
