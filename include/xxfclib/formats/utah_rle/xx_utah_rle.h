/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/sarnold/urt/master/lib/rle_getrow.c, https://brlcad.org/OLD/doxygen/d6/d94/rle__code_8h-source.html
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_UTAH_RLE_H
#define XX_UTAH_RLE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_utah_rle {
    Abstractformat format;
} xx_utah_rle;
XXFC_API void xx_utah_rle_init(xx_utah_rle *, xx_io_device *, int64_t);
XXFC_API xx_utah_rle *xx_utah_rle_create(xx_io_device *, int64_t);
XXFC_API void xx_utah_rle_destroy(xx_utah_rle *);
XXFC_API void xx_utah_rle_free(xx_utah_rle *);
XXFC_API bool xx_utah_rle_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_utah_rle_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_utah_rle_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_utah_rle_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_utah_rle_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_utah_rle_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_utah_rle_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
