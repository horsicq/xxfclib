/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://raw.githubusercontent.com/uclouvain/openjpeg/master/src/lib/openjp2/jp2.c,
 * https://raw.githubusercontent.com/uclouvain/openjpeg/master/src/lib/openjp2/j2k.c Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_JPEG2000_JP2_H
#define XX_JPEG2000_JP2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_jpeg2000_jp2 {
    Abstractformat format;
} xx_jpeg2000_jp2;
XXFC_API void xx_jpeg2000_jp2_init(xx_jpeg2000_jp2 *, xx_io_device *, int64_t);
XXFC_API xx_jpeg2000_jp2 *xx_jpeg2000_jp2_create(xx_io_device *, int64_t);
XXFC_API void xx_jpeg2000_jp2_destroy(xx_jpeg2000_jp2 *);
XXFC_API void xx_jpeg2000_jp2_free(xx_jpeg2000_jp2 *);
XXFC_API bool xx_jpeg2000_jp2_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_jpeg2000_jp2_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_jpeg2000_jp2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_jpeg2000_jp2_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_jpeg2000_jp2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_jpeg2000_jp2_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_jpeg2000_jp2_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
