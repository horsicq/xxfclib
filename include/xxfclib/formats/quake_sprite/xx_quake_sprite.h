/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake/master/WinQuake/spritegn.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_QUAKE_SPRITE_H
#define XX_QUAKE_SPRITE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_quake_sprite { Abstractformat format; } xx_quake_sprite;
XXFC_API void xx_quake_sprite_init(xx_quake_sprite *,xx_io_device *,int64_t);
XXFC_API xx_quake_sprite *xx_quake_sprite_create(xx_io_device *,int64_t);
XXFC_API void xx_quake_sprite_destroy(xx_quake_sprite *);
XXFC_API void xx_quake_sprite_free(xx_quake_sprite *);
XXFC_API bool xx_quake_sprite_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_quake_sprite_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_quake_sprite_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_quake_sprite_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_quake_sprite_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_quake_sprite_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_quake_sprite_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
