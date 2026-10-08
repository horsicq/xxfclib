/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/nipy/nibabel/master/nibabel/freesurfer/io.py
 * FreeSurfer binary triangle surfaces: complete creator lines, finite BE32 vertices, bounded nondegenerate triangle indexes and optional typed volume information. Original descriptors/geometry exported; old quad formats and rendering unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_FREESURFER_SURFACE_H
#define XX_FREESURFER_SURFACE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_freesurfer_surface {Abstractformat format;} xx_freesurfer_surface;
XXFC_API void xx_freesurfer_surface_init(xx_freesurfer_surface *,xx_io_device *,int64_t);
XXFC_API xx_freesurfer_surface *xx_freesurfer_surface_create(xx_io_device *,int64_t);
XXFC_API void xx_freesurfer_surface_destroy(xx_freesurfer_surface *);
XXFC_API void xx_freesurfer_surface_free(xx_freesurfer_surface *);
XXFC_API bool xx_freesurfer_surface_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_freesurfer_surface_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_freesurfer_surface_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_freesurfer_surface_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_freesurfer_surface_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
