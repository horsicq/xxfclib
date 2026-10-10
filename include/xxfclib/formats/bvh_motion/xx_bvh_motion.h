/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/BVH/BVHLoader.cpp
 * Biovision BVH: complete single-root hierarchy with bounded joints/end sites, unique channels, finite offsets and exact counted finite motion frames. Original hierarchy
 * and motion table exported; multiple roots and nonstandard channels declined. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_BVH_MOTION_H
#define XX_BVH_MOTION_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bvh_motion {
    Abstractformat format;
} xx_bvh_motion;
XXFC_API void xx_bvh_motion_init(xx_bvh_motion *, xx_io_device *, int64_t);
XXFC_API xx_bvh_motion *xx_bvh_motion_create(xx_io_device *, int64_t);
XXFC_API void xx_bvh_motion_destroy(xx_bvh_motion *);
XXFC_API void xx_bvh_motion_free(xx_bvh_motion *);
XXFC_API bool xx_bvh_motion_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_bvh_motion_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_bvh_motion_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_bvh_motion_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_bvh_motion_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_bvh_motion_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_bvh_motion_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
