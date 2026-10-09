/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/DiscIO/FileSystemGCWii.cpp
 * GameCube disc images with bounded DOL and FST. Exports DOL, optional apploader and numbered FST files. Directory/name bounds and nested directory ranges checked; Wii encrypted partitions, rendering and execution unsupported.
 */
#ifndef XX_NINTENDO_GCM_H
#define XX_NINTENDO_GCM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_gcm { Abstractformat format; } xx_nintendo_gcm;
XXFC_API void xx_nintendo_gcm_init(xx_nintendo_gcm *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_gcm *xx_nintendo_gcm_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_gcm_destroy(xx_nintendo_gcm *);
XXFC_API void xx_nintendo_gcm_free(xx_nintendo_gcm *);
XXFC_API bool xx_nintendo_gcm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_gcm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_gcm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_gcm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_gcm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_gcm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_gcm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
