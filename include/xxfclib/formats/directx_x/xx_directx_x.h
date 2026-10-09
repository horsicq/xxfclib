/* SPDX-License-Identifier: MIT
 * Primary reference: https://learn.microsoft.com/en-us/windows/win32/direct3d9/dx9-graphics-reference-x-file-format
 * Microsoft X text0303/32-bit meshes: complete standard typed frames/materials/mesh data with finite values and bounded resolved indexes, including bounded XSkinMeshHeader and SkinWeights records. Recognized standard templates are validated; original top-level templates and objects exported without skinning evaluation. Compressed/binary/animation/custom templates declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_DIRECTX_X_H
#define XX_DIRECTX_X_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_directx_x {Abstractformat format;} xx_directx_x;
XXFC_API void xx_directx_x_init(xx_directx_x *,xx_io_device *,int64_t);
XXFC_API xx_directx_x *xx_directx_x_create(xx_io_device *,int64_t);
XXFC_API void xx_directx_x_destroy(xx_directx_x *);
XXFC_API void xx_directx_x_free(xx_directx_x *);
XXFC_API bool xx_directx_x_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_directx_x_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_directx_x_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_directx_x_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_directx_x_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_directx_x_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_directx_x_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
