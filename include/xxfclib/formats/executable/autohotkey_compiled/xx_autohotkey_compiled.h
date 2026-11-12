/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * AutoHotkey compiled scripts (Ahk2Exe): the script and every FileInstall'd
 * file travel as RCDATA resources, the files named by their source path.
 */
#ifndef XX_AUTOHOTKEY_COMPILED_H
#define XX_AUTOHOTKEY_COMPILED_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_autohotkey_compiled {
    Abstractformat format;
} xx_autohotkey_compiled;
XXFC_API void xx_autohotkey_compiled_init(xx_autohotkey_compiled *, xx_io_device *, int64_t);
XXFC_API xx_autohotkey_compiled *xx_autohotkey_compiled_create(xx_io_device *, int64_t);
XXFC_API void xx_autohotkey_compiled_destroy(xx_autohotkey_compiled *);
XXFC_API void xx_autohotkey_compiled_free(xx_autohotkey_compiled *);
XXFC_API bool xx_autohotkey_compiled_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_autohotkey_compiled_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_autohotkey_compiled_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_autohotkey_compiled_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_autohotkey_compiled_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_autohotkey_compiled_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_autohotkey_compiled_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
