/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_MICROSOFT_LIT_H
#define XX_MICROSOFT_LIT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Microsoft Reader ITOLITLS books. Converts binary HTML/OPF and extracts CSS
 * and images through a separately licensed, bounded RAM-only ConvertLIT helper.
 * Supports unencrypted books and embedded-key DRM1/DRM3; owner-key DRM5 is
 * unsupported. Input and decoded book are limited to 64 MiB each. No temporary
 * files, key files, input execution or host filesystem access by the decoder. */
typedef struct xx_microsoft_lit {
    Abstractformat format;
    void *index;
    uint8_t *decoded;
    size_t decoded_size;
    uint64_t decode_memory_limit;
    uint32_t drm_level;
} xx_microsoft_lit;
XXFC_API void xx_microsoft_lit_init(xx_microsoft_lit *, xx_io_device *, int64_t);
XXFC_API xx_microsoft_lit *xx_microsoft_lit_create(xx_io_device *, int64_t);
XXFC_API void xx_microsoft_lit_destroy(xx_microsoft_lit *);
XXFC_API void xx_microsoft_lit_free(xx_microsoft_lit *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_microsoft_lit_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_microsoft_lit_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_microsoft_lit_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_microsoft_lit_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_microsoft_lit_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
