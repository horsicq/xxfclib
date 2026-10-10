/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_COMMODORE_TAP_H
#define XX_COMMODORE_TAP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_commodore_tap {
    Abstractformat format;
} xx_commodore_tap;
XXFC_API void xx_commodore_tap_init(xx_commodore_tap *, xx_io_device *, int64_t);
XXFC_API xx_commodore_tap *xx_commodore_tap_create(xx_io_device *, int64_t);
XXFC_API void xx_commodore_tap_destroy(xx_commodore_tap *);
XXFC_API void xx_commodore_tap_free(xx_commodore_tap *);
XXFC_API bool xx_commodore_tap_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_commodore_tap_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_commodore_tap_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_commodore_tap_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_commodore_tap_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_commodore_tap_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_commodore_tap_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
