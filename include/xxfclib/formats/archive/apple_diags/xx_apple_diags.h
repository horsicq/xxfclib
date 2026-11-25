/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_APPLE_DIAGS_H
#define XXFCLIB_FORMAT_APPLE_DIAGS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Apple Hardware Test |_|_ LOC envelope: one LZSS/XOR script, Adler-32 checked.
 * The envelope stores no filename; the single member is named script.txt. */
XXFC_API Abstractformat *xx_apple_diags_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_apple_diags_free(Abstractformat *format);
XXFC_API bool xx_apple_diags_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_apple_diags_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_apple_diags_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_apple_diags_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_apple_diags_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
