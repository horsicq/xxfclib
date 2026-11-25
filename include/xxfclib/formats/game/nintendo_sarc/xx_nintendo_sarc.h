/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/KillzXGaming/Switch-Toolbox/blob/master/File_Format_Library/FileFormats/Archives/SARC.cs
 * Big/little endian SARC v1 stored files; numeric names avoid unsafe paths. Yaz0-wrapped SARC must be decoded separately.
 */
#ifndef XX_NINTENDO_SARC_H
#define XX_NINTENDO_SARC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_sarc {
    Abstractformat format;
} xx_nintendo_sarc;
XXFC_API void xx_nintendo_sarc_init(xx_nintendo_sarc *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_sarc *xx_nintendo_sarc_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_sarc_destroy(xx_nintendo_sarc *);
XXFC_API void xx_nintendo_sarc_free(xx_nintendo_sarc *);
XXFC_API bool xx_nintendo_sarc_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_sarc_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_sarc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_sarc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_sarc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_sarc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_sarc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
