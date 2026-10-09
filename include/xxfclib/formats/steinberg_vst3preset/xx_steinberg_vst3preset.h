/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/steinbergmedia/vst3_public_sdk/master/source/vst/vstpresetfile.cpp
 * VST3 preset version1 UID and complete List directory of unique Comp/Cont/Info chunks with disjoint exact extents. Original opaque plugin states/XML bytes exported; plugin loading, state interpretation and unknown chunk IDs are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_STEINBERG_VST3PRESET_H
#define XX_STEINBERG_VST3PRESET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_steinberg_vst3preset { Abstractformat format; } xx_steinberg_vst3preset;
XXFC_API void xx_steinberg_vst3preset_init(xx_steinberg_vst3preset *,xx_io_device *,int64_t);
XXFC_API xx_steinberg_vst3preset *xx_steinberg_vst3preset_create(xx_io_device *,int64_t);
XXFC_API void xx_steinberg_vst3preset_destroy(xx_steinberg_vst3preset *);
XXFC_API void xx_steinberg_vst3preset_free(xx_steinberg_vst3preset *);
XXFC_API bool xx_steinberg_vst3preset_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_steinberg_vst3preset_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_steinberg_vst3preset_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_steinberg_vst3preset_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_steinberg_vst3preset_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_steinberg_vst3preset_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_steinberg_vst3preset_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
