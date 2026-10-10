/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/magcius/noclip.website/main/src/Common/NW4R/lyt/Layout.ts
 * Big-endian RLYT version8 with lyt1, basic pan1 pane hierarchies, texture/font lists and groups. Checks section framing, finite pane geometry, bounded strings and
 * balanced hierarchy markers. Exports individual encoded sections; picture/text/window/material extensions, external resources and rendering unsupported.
 */
#ifndef XX_NINTENDO_BRLYT_H
#define XX_NINTENDO_BRLYT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_brlyt {
    Abstractformat format;
} xx_nintendo_brlyt;
XXFC_API void xx_nintendo_brlyt_init(xx_nintendo_brlyt *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_brlyt *xx_nintendo_brlyt_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_brlyt_destroy(xx_nintendo_brlyt *);
XXFC_API void xx_nintendo_brlyt_free(xx_nintendo_brlyt *);
XXFC_API bool xx_nintendo_brlyt_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_brlyt_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_brlyt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_brlyt_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_brlyt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_brlyt_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_brlyt_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
