/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/devkitPro/3dstools/master/src/3dsxtool.cpp
 * 3DSX version0/flags0 with standard32-byte header and8-byte relocation headers, up to256MiB combined segments. Exports code/rodata/stored-data and bounded relocation
 * tables. Extended SMDH/RomFS header, unknown relocation types, loading/relocation and execution unsupported.
 */
#ifndef XX_NINTENDO_3DSX_H
#define XX_NINTENDO_3DSX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_3dsx {
    Abstractformat format;
} xx_nintendo_3dsx;
XXFC_API void xx_nintendo_3dsx_init(xx_nintendo_3dsx *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_3dsx *xx_nintendo_3dsx_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_3dsx_destroy(xx_nintendo_3dsx *);
XXFC_API void xx_nintendo_3dsx_free(xx_nintendo_3dsx *);
XXFC_API bool xx_nintendo_3dsx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_3dsx_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_3dsx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_3dsx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_3dsx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_3dsx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_3dsx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
