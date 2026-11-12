/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * SumatraPDF installers: the program files travel in an "LzSA" archive
 * (Sumatra's own simple LZMA archive) kept as a resource of the installer.
 */
#ifndef XX_SUMATRA_LZSA_H
#define XX_SUMATRA_LZSA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sumatra_lzsa {
    Abstractformat format;
} xx_sumatra_lzsa;
XXFC_API void xx_sumatra_lzsa_init(xx_sumatra_lzsa *, xx_io_device *, int64_t);
XXFC_API xx_sumatra_lzsa *xx_sumatra_lzsa_create(xx_io_device *, int64_t);
XXFC_API void xx_sumatra_lzsa_destroy(xx_sumatra_lzsa *);
XXFC_API void xx_sumatra_lzsa_free(xx_sumatra_lzsa *);
XXFC_API bool xx_sumatra_lzsa_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sumatra_lzsa_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sumatra_lzsa_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sumatra_lzsa_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sumatra_lzsa_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sumatra_lzsa_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sumatra_lzsa_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
