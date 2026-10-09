/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_PYC_H
#define XXFCLIB_FORMAT_PYC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_pyc {
    Abstractformat format;
    uint16_t magic;
    char version[24];
    uint32_t flags, header_size;
    int64_t marshal_offset, marshal_size;
    xx_list_s constants; /* Owned UTF-8 char *; free through destroy/free. */
    bool analyzed;
} xx_pyc;

XXFC_API void xx_pyc_init(xx_pyc *pyc, xx_io_device *device, int64_t base_address);
XXFC_API xx_pyc *xx_pyc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pyc_destroy(xx_pyc *pyc);
XXFC_API void xx_pyc_free(xx_pyc *pyc);
XXFC_API bool xx_pyc_is_known_magic(uint16_t magic);
XXFC_API bool xx_pyc_check_magic(const uint8_t *data, size_t size);
/* Inspection preserves a recognized header/version even when its marshal
 * body is damaged. Strict callbacks and carving require a complete module. */
XXFC_API bool xx_pyc_analyze(xx_pyc *pyc, xx_pd_struct *pd);
XXFC_API bool xx_pyc_const_present(const xx_pyc *pyc, const char *value);
XXFC_API bool xx_pyc_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_pyc_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_pyc_get_format_size(Abstractformat *format, xx_pd_struct *pd);
static inline Abstractformat *xx_pyc_to_format(xx_pyc *pyc) { return pyc ? &pyc->format : NULL; }
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pyc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pyc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pyc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pyc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pyc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
