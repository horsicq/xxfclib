/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://fits.gsfc.nasa.gov/standard40/fits_standard40aa-le.pdf
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_FITS_H
#define XX_FITS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_fits { Abstractformat format; } xx_fits;
XXFC_API void xx_fits_init(xx_fits *,xx_io_device *,int64_t);
XXFC_API xx_fits *xx_fits_create(xx_io_device *,int64_t);
XXFC_API void xx_fits_destroy(xx_fits *);
XXFC_API void xx_fits_free(xx_fits *);
XXFC_API bool xx_fits_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_fits_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_fits_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_fits_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_fits_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
