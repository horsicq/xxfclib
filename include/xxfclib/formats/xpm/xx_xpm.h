/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.xfree86.org/current/xpm.pdf
 * Stored encoded component extraction; no image rendering or execution.
 */
#ifndef XX_XPM_H
#define XX_XPM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xpm {
    Abstractformat format;
} xx_xpm;
XXFC_API void xx_xpm_init(xx_xpm *, xx_io_device *, int64_t);
XXFC_API xx_xpm *xx_xpm_create(xx_io_device *, int64_t);
XXFC_API void xx_xpm_destroy(xx_xpm *);
XXFC_API void xx_xpm_free(xx_xpm *);
XXFC_API bool xx_xpm_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_xpm_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_xpm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_xpm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_xpm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_xpm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_xpm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
