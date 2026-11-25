/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/kode54/psflib/master/psflib.c
 * PSF1 nonempty program wrappers with bounded reserved/program/tag ranges, zlib framing and compressed-program CRC32. Exports reserved bytes, encoded program.zlib and
 * tag text; no inflation, library resolution, audio emulation or execution.
 */
#ifndef XX_SONY_PSF_H
#define XX_SONY_PSF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_psf {
    Abstractformat format;
} xx_sony_psf;
XXFC_API void xx_sony_psf_init(xx_sony_psf *, xx_io_device *, int64_t);
XXFC_API xx_sony_psf *xx_sony_psf_create(xx_io_device *, int64_t);
XXFC_API void xx_sony_psf_destroy(xx_sony_psf *);
XXFC_API void xx_sony_psf_free(xx_sony_psf *);
XXFC_API bool xx_sony_psf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sony_psf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sony_psf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sony_psf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sony_psf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sony_psf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sony_psf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
