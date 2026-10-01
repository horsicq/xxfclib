/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/OGRECave/ogre/master/OgreMain/src/OgreSkeletonSerializer.cpp
 * OGRE skeleton serializer v1.10/v1.80: complete bounded bone/hierarchy/animation track/keyframe chunks with finite transforms, resolved bone handles and acyclic parent links. Original typed chunks exported; external animation links, animation base-keyframe extensions and unknown chunks declined. Bone-name bytes excluded from the historical declared bone chunk length are validated and retained.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_OGRE_SKELETON_H
#define XX_OGRE_SKELETON_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ogre_skeleton {Abstractformat format;} xx_ogre_skeleton;
XXFC_API void xx_ogre_skeleton_init(xx_ogre_skeleton *,xx_io_device *,int64_t);
XXFC_API xx_ogre_skeleton *xx_ogre_skeleton_create(xx_io_device *,int64_t);
XXFC_API void xx_ogre_skeleton_destroy(xx_ogre_skeleton *);
XXFC_API void xx_ogre_skeleton_free(xx_ogre_skeleton *);
XXFC_API bool xx_ogre_skeleton_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ogre_skeleton_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
