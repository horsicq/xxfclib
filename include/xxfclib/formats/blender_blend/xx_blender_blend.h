/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/blender/blender/blob/main/doc/blender_file_format/mystery_of_the_blend.html
 * Uncompressed classic BLENDER headers, pointer widths32/64 and either byte order, versions250-499. Parses full BHead framing, exactly one bounded SDNA dictionary with all type/name/structure references, and terminating ENDB. Up to4096 blocks and64MiB file. Exports original encoded blocks; DNA field interpretation, pointer relocation, newer blend variants, compression and rendering unsupported.
 */
#ifndef XX_BLENDER_BLEND_H
#define XX_BLENDER_BLEND_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_blender_blend { Abstractformat format; } xx_blender_blend;
XXFC_API void xx_blender_blend_init(xx_blender_blend *,xx_io_device *,int64_t);
XXFC_API xx_blender_blend *xx_blender_blend_create(xx_io_device *,int64_t);
XXFC_API void xx_blender_blend_destroy(xx_blender_blend *);
XXFC_API void xx_blender_blend_free(xx_blender_blend *);
XXFC_API bool xx_blender_blend_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_blender_blend_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_blender_blend_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_blender_blend_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_blender_blend_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_blender_blend_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_blender_blend_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
