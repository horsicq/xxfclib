/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://learn.microsoft.com/en-us/previous-versions/ms997538(v=msdn.10)
 * Publishes stored payload components; see docs/registered_second_fifty_formats.md.
 */
#ifndef XX_ICO_H
#define XX_ICO_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ico {
    Abstractformat format;
} xx_ico;
XXFC_API void xx_ico_init(xx_ico *, xx_io_device *, int64_t);
XXFC_API xx_ico *xx_ico_create(xx_io_device *, int64_t);
XXFC_API void xx_ico_destroy(xx_ico *);
XXFC_API void xx_ico_free(xx_ico *);
XXFC_API bool xx_ico_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ico_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ico_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ico_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ico_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ico_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ico_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
