/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_CHROMIUM_PAK_H
#define XX_CHROMIUM_PAK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Chromium DataPack version 4/5. Exports each numeric resource, including
 * version 5 alias resources. Binary payloads retain their exact bytes; text
 * payloads retain the original UTF-8/UTF-16LE encoding. No temporary files. */
typedef struct xx_chromium_pak {
    Abstractformat format;
    void *index;
    uint32_t version, encoding;
} xx_chromium_pak;
XXFC_API void xx_chromium_pak_init(xx_chromium_pak *, xx_io_device *, int64_t);
XXFC_API xx_chromium_pak *xx_chromium_pak_create(xx_io_device *, int64_t);
XXFC_API void xx_chromium_pak_destroy(xx_chromium_pak *);
XXFC_API void xx_chromium_pak_free(xx_chromium_pak *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_chromium_pak_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_chromium_pak_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_chromium_pak_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_chromium_pak_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_chromium_pak_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
