/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/DjVuLibre/djvulibre/master/libdjvu/IFFByteStream.cpp,
 * https://raw.githubusercontent.com/DjVuLibre/djvulibre/master/libdjvu/DjVuInfo.cpp Stored encoded components only; no rendering or external-resource access.
 */
#ifndef XX_DJVU_H
#define XX_DJVU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_djvu {
    Abstractformat format;
} xx_djvu;
XXFC_API void xx_djvu_init(xx_djvu *, xx_io_device *, int64_t);
XXFC_API xx_djvu *xx_djvu_create(xx_io_device *, int64_t);
XXFC_API void xx_djvu_destroy(xx_djvu *);
XXFC_API void xx_djvu_free(xx_djvu *);
XXFC_API bool xx_djvu_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_djvu_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_djvu_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_djvu_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_djvu_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_djvu_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_djvu_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
