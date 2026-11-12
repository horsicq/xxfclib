/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_PKLITE_H
#define XXFCLIB_FORMAT_PKLITE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Static, non-executing recovery of early 1.00--1.05, non-extra PKLITE MZ.
 * Preserves a consistent saved header; otherwise builds a documented MZ
 * header from the decoded code, relocations, loader AX and register footer.
 * Other loader families, extra compression, nonzero tails and overlays fail. */
typedef struct xx_pklite { Abstractformat format; } xx_pklite;
XXFC_API void xx_pklite_init(xx_pklite *,xx_io_device *,int64_t);
XXFC_API void xx_pklite_destroy(xx_pklite *);
XXFC_API xx_pklite *xx_pklite_create(xx_io_device *,int64_t);
XXFC_API void xx_pklite_free(xx_pklite *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pklite_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pklite_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pklite_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pklite_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pklite_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
