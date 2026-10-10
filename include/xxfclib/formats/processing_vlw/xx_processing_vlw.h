/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/processing/processing4/main/core/src/processing/core/PFont.java
 * Processing VLW11 with complete ordered glyph metrics, exact grayscale bitmap array, modified-UTF name records and smoothing flag. Original glyph bitmaps and names
 * exported; older layouts, external font loading and rendering are unsupported. Signatureless detection is offset-zero only. Limit64MiB,4096 components. No payload or
 * external resource is executed.
 */
#ifndef XX_PROCESSING_VLW_H
#define XX_PROCESSING_VLW_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_processing_vlw {
    Abstractformat format;
} xx_processing_vlw;
XXFC_API void xx_processing_vlw_init(xx_processing_vlw *, xx_io_device *, int64_t);
XXFC_API xx_processing_vlw *xx_processing_vlw_create(xx_io_device *, int64_t);
XXFC_API void xx_processing_vlw_destroy(xx_processing_vlw *);
XXFC_API void xx_processing_vlw_free(xx_processing_vlw *);
XXFC_API bool xx_processing_vlw_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_processing_vlw_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_processing_vlw_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_processing_vlw_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_processing_vlw_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_processing_vlw_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_processing_vlw_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
