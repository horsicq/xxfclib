/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/mp3butcher/Cal3D/master/cal3d/src/cal3d/loader.cpp
 * Cal3D CSF version700 skeleton: complete counted UTF8 bone names, finite local/inverse-bind transforms, resolved reciprocal parent/child IDs and acyclic hierarchy. Original descriptor and individual bone records exported; later extension versions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_CAL3D_SKELETON_H
#define XX_CAL3D_SKELETON_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cal3d_skeleton {Abstractformat format;} xx_cal3d_skeleton;
XXFC_API void xx_cal3d_skeleton_init(xx_cal3d_skeleton *,xx_io_device *,int64_t);
XXFC_API xx_cal3d_skeleton *xx_cal3d_skeleton_create(xx_io_device *,int64_t);
XXFC_API void xx_cal3d_skeleton_destroy(xx_cal3d_skeleton *);
XXFC_API void xx_cal3d_skeleton_free(xx_cal3d_skeleton *);
XXFC_API bool xx_cal3d_skeleton_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cal3d_skeleton_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cal3d_skeleton_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cal3d_skeleton_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cal3d_skeleton_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cal3d_skeleton_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cal3d_skeleton_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
