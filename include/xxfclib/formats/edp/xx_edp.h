/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_EDP_H
#define XXFCLIB_FORMAT_EDP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_edp {
    Abstractformat format;
} xx_edp;

XXFC_API void xx_edp_init(xx_edp *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_edp *xx_edp_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_edp_destroy(xx_edp *reader);
XXFC_API void xx_edp_free(xx_edp *reader);
XXFC_API bool xx_edp_check_is_valid(Abstractformat *format, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_edp_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_edp_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_edp_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_edp_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_edp_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
