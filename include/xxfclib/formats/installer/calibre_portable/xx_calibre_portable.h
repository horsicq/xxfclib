/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * calibre portable installer: the whole "Calibre Portable" tree as a stored
 * ZIP, compressed as one lzip member and kept in the "EXTRA" resource.
 */
#ifndef XX_CALIBRE_PORTABLE_H
#define XX_CALIBRE_PORTABLE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_calibre_portable {
    Abstractformat format;
    void *nested; /* The decoded payload's ZIP reader. */
} xx_calibre_portable;
XXFC_API void xx_calibre_portable_init(xx_calibre_portable *, xx_io_device *, int64_t);
XXFC_API xx_calibre_portable *xx_calibre_portable_create(xx_io_device *, int64_t);
XXFC_API void xx_calibre_portable_destroy(xx_calibre_portable *);
XXFC_API void xx_calibre_portable_free(xx_calibre_portable *);
XXFC_API bool xx_calibre_portable_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_calibre_portable_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_calibre_portable_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_calibre_portable_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_calibre_portable_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_calibre_portable_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_calibre_portable_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
