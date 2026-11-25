/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/LWS/LWSLoader.cpp
 * LightWave LWSC2 legacy scene subset: complete typed scene/render/view settings, object/light/camera motion channels with counted ordered finite keys and pre/post
 * behaviors. Original descriptor/item motion/settings records exported; known built-in empty DistantLight/Perspective plugin blocks accepted; geometry filenames remain
 * metadata, no sidecars loaded; unknown plugins/commands declined. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_LIGHTWAVE_SCENE_H
#define XX_LIGHTWAVE_SCENE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lightwave_scene {
    Abstractformat format;
} xx_lightwave_scene;
XXFC_API void xx_lightwave_scene_init(xx_lightwave_scene *, xx_io_device *, int64_t);
XXFC_API xx_lightwave_scene *xx_lightwave_scene_create(xx_io_device *, int64_t);
XXFC_API void xx_lightwave_scene_destroy(xx_lightwave_scene *);
XXFC_API void xx_lightwave_scene_free(xx_lightwave_scene *);
XXFC_API bool xx_lightwave_scene_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_lightwave_scene_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lightwave_scene_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_lightwave_scene_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lightwave_scene_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lightwave_scene_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_lightwave_scene_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
