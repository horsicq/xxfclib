/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://developer.apple.com/library/archive/documentation/MusicAudio/Reference/CAFSpec/CAF_spec/CAF_spec.html
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_CAF_H
#define XX_CAF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_caf {
    Abstractformat format;
} xx_caf;
XXFC_API void xx_caf_init(xx_caf *, xx_io_device *, int64_t);
XXFC_API xx_caf *xx_caf_create(xx_io_device *, int64_t);
XXFC_API void xx_caf_destroy(xx_caf *);
XXFC_API void xx_caf_free(xx_caf *);
XXFC_API bool xx_caf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_caf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_caf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_caf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_caf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_caf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_caf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
