/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * 360 PalmInput (360 输入法) setup: the installed files travel in the
 * "COMPRESSEDDATA" resource as LZMA groups, one per wildcard pattern.
 */
#ifndef XX_PALMINPUT_360_H
#define XX_PALMINPUT_360_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_palminput_360 {
    Abstractformat format;
    void *cache; /* The last decoded group. */
} xx_palminput_360;
XXFC_API void xx_palminput_360_init(xx_palminput_360 *, xx_io_device *, int64_t);
XXFC_API xx_palminput_360 *xx_palminput_360_create(xx_io_device *, int64_t);
XXFC_API void xx_palminput_360_destroy(xx_palminput_360 *);
XXFC_API void xx_palminput_360_free(xx_palminput_360 *);
XXFC_API bool xx_palminput_360_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_palminput_360_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_palminput_360_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_palminput_360_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_palminput_360_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_palminput_360_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_palminput_360_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
