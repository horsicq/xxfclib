/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.web3d.org/documents/specifications/14772/V2.0/part1/nodesRef.html
 * VRML97 bounded complete static node subset: Shape/Appearance/Material/IndexedFaceSet/Coordinate/Group/Transform and basic primitive/view/light metadata, typed fields, finite vectors, bounded indexes, legal node contexts, normalized rotation axes and balanced structure. Original encoded root nodes exported. DEF/USE, scripting, PROTO/ROUTE, textures and other nodes/fields declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_VRML_SCENE_H
#define XX_VRML_SCENE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_vrml_scene {Abstractformat format;} xx_vrml_scene;
XXFC_API void xx_vrml_scene_init(xx_vrml_scene *,xx_io_device *,int64_t);
XXFC_API xx_vrml_scene *xx_vrml_scene_create(xx_io_device *,int64_t);
XXFC_API void xx_vrml_scene_destroy(xx_vrml_scene *);
XXFC_API void xx_vrml_scene_free(xx_vrml_scene *);
XXFC_API bool xx_vrml_scene_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_vrml_scene_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_vrml_scene_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_vrml_scene_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_vrml_scene_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_vrml_scene_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_vrml_scene_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
