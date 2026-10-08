/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMATS_PPD_XX_PPD_H
#define XXFCLIB_FORMATS_PPD_XX_PPD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Paranoid Productions PPD resource archive. */
typedef struct xx_ppd {
    Abstractformat format;
} xx_ppd;

XXFC_API void xx_ppd_init(xx_ppd *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_ppd *xx_ppd_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ppd_destroy(xx_ppd *archive);
XXFC_API void xx_ppd_free(xx_ppd *archive);
XXFC_API bool xx_ppd_check_is_valid(Abstractformat *format,
                                   xx_pd_struct *pd);
XXFC_API bool xx_ppd_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ppd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ppd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ppd_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
