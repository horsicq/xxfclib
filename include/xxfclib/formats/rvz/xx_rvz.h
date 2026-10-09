/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_RVZ_H
#define XXFCLIB_FORMAT_RVZ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Dolphin RVZ v1 GameCube images, exposed as a reconstructed ISO member. */
typedef struct xx_rvz {
    Abstractformat format;
} xx_rvz;

XXFC_API void xx_rvz_init(xx_rvz *reader, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_rvz *xx_rvz_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rvz_destroy(xx_rvz *reader);
XXFC_API void xx_rvz_free(xx_rvz *reader);
XXFC_API bool xx_rvz_check_is_valid(Abstractformat *format, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_rvz_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_rvz_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_rvz_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_rvz_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_rvz_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
