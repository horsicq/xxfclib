/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_EID_H
#define XXFCLIB_FORMAT_EID_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Encrypted EID archive reader. Built-in master keys recover the archive
 * key; payloads are decoded and checksum-validated during test/extraction. */
XXFC_API Abstractformat *xx_eid_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_eid_free(Abstractformat *format);
XXFC_API bool xx_eid_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_eid_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_eid_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_eid_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_eid_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
#endif
