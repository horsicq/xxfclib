/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_ZX_SPECTRUM_SZX_H
#define XX_ZX_SPECTRUM_SZX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_zx_spectrum_szx {
    Abstractformat format;
} xx_zx_spectrum_szx;
XXFC_API void xx_zx_spectrum_szx_init(xx_zx_spectrum_szx *, xx_io_device *, int64_t);
XXFC_API xx_zx_spectrum_szx *xx_zx_spectrum_szx_create(xx_io_device *, int64_t);
XXFC_API void xx_zx_spectrum_szx_destroy(xx_zx_spectrum_szx *);
XXFC_API void xx_zx_spectrum_szx_free(xx_zx_spectrum_szx *);
XXFC_API bool xx_zx_spectrum_szx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_zx_spectrum_szx_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_zx_spectrum_szx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_zx_spectrum_szx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_zx_spectrum_szx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_zx_spectrum_szx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_zx_spectrum_szx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
