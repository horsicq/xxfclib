/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/KillzXGaming/BfresLibrary/master/BfresLibrary/WiiU/ResFileParser.cs
 * Wii U big-endian FRES versions2.4 through4.x, attachment-only files with up to1024 external resources. Validates relative dictionary/name/data pointers and declared string pool. Exports attachment bytes; models/textures/animations, Switch FRES, relocation and rendering unsupported.
 */
#ifndef XX_NINTENDO_BFRES_H
#define XX_NINTENDO_BFRES_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bfres { Abstractformat format; } xx_nintendo_bfres;
XXFC_API void xx_nintendo_bfres_init(xx_nintendo_bfres *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bfres *xx_nintendo_bfres_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bfres_destroy(xx_nintendo_bfres *);
XXFC_API void xx_nintendo_bfres_free(xx_nintendo_bfres *);
XXFC_API bool xx_nintendo_bfres_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bfres_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_bfres_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_bfres_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_bfres_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_bfres_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_bfres_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
