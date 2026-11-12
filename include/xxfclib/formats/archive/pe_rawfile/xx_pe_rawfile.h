/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_PE_RAWFILE_H
#define XXFCLIB_FORMAT_PE_RAWFILE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pe_rawfile { Abstractformat format; } xx_pe_rawfile;
XXFC_API void xx_pe_rawfile_init(xx_pe_rawfile *,xx_io_device *,int64_t);
XXFC_API void xx_pe_rawfile_destroy(xx_pe_rawfile *);
XXFC_API xx_pe_rawfile *xx_pe_rawfile_create(xx_io_device *,int64_t);
XXFC_API void xx_pe_rawfile_free(xx_pe_rawfile *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pe_rawfile_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pe_rawfile_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pe_rawfile_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pe_rawfile_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pe_rawfile_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
