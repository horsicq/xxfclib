/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/renpy/renpy/master/renpy/loader.py
 * RPA3 with zlib index and restricted protocol2 pickle dictionary of one (offset,length) tuple per file. Decodes the index without executing pickle opcodes. Prefix
 * bytes, chunk lists, object construction/memo references and other pickle protocols rejected; index scratch capped at12MiB and any lower explicit budget.
 */
#ifndef XX_RENPY_RPA_H
#define XX_RENPY_RPA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_renpy_rpa {
    Abstractformat format;
} xx_renpy_rpa;
XXFC_API void xx_renpy_rpa_init(xx_renpy_rpa *, xx_io_device *, int64_t);
XXFC_API xx_renpy_rpa *xx_renpy_rpa_create(xx_io_device *, int64_t);
XXFC_API void xx_renpy_rpa_destroy(xx_renpy_rpa *);
XXFC_API void xx_renpy_rpa_free(xx_renpy_rpa *);
XXFC_API bool xx_renpy_rpa_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_renpy_rpa_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_renpy_rpa_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_renpy_rpa_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_renpy_rpa_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_renpy_rpa_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_renpy_rpa_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
