/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMATS_DFC_XX_DFC_H
#define XXFCLIB_FORMATS_DFC_XX_DFC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** DFC disk archive with stored and PKWARE DCL members. */
typedef struct xx_dfc {
    Abstractformat format;
} xx_dfc;

XXFC_API void xx_dfc_init(xx_dfc *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_dfc *xx_dfc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_dfc_destroy(xx_dfc *archive);
XXFC_API void xx_dfc_free(xx_dfc *archive);
XXFC_API bool xx_dfc_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_dfc_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dfc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dfc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dfc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dfc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dfc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
