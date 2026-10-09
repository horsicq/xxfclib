/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://raw.githubusercontent.com/axiomatic-systems/Bento4/master/Source/C%2B%2B/Core/Ap4Atom.cpp, https://raw.githubusercontent.com/axiomatic-systems/Bento4/master/Source/C%2B%2B/Core/Ap4AtomFactory.cpp
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_MP4_H
#define XX_MP4_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mp4 { Abstractformat format; } xx_mp4;
XXFC_API void xx_mp4_init(xx_mp4 *,xx_io_device *,int64_t);
XXFC_API xx_mp4 *xx_mp4_create(xx_io_device *,int64_t);
XXFC_API void xx_mp4_destroy(xx_mp4 *);
XXFC_API void xx_mp4_free(xx_mp4 *);
XXFC_API bool xx_mp4_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mp4_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mp4_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mp4_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mp4_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mp4_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mp4_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
