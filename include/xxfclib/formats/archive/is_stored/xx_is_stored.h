/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_IS_STORED_H
#define XXFCLIB_FORMAT_IS_STORED_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Fixed XOR A9 and checksum-verified positional PE wrappers. */
typedef struct xx_is_stored {
    Abstractformat format;
} xx_is_stored;
XXFC_API void xx_is_stored_init(xx_is_stored *, xx_io_device *, int64_t);
XXFC_API xx_is_stored *xx_is_stored_create(xx_io_device *, int64_t);
XXFC_API void xx_is_stored_destroy(xx_is_stored *);
XXFC_API void xx_is_stored_free(xx_is_stored *);
XXFC_API bool xx_is_stored_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_is_stored_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_is_stored_detect(xx_io_device *, int64_t);
XXFC_API bool xx_is_stored_unpack_to_device(xx_is_stored *, xx_io_device *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_is_stored_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_is_stored_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_is_stored_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_is_stored_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_is_stored_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
