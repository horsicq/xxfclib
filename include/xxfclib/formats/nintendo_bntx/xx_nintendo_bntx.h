/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/aboood40091/BNTX-Injector/master/structs.py
 * Little-endian BNTX versions0.4.0.0/0.4.1.0 NX 2D single-layer textures; exports bounded encoded mip slices. Validates BRTI/BRTD pointers, names, dimensions and mip extents. No sparse/multisample/array/runtime-relocated variants, swizzle reversal or pixel decoding; relocation bytes are not applied.
 */
#ifndef XX_NINTENDO_BNTX_H
#define XX_NINTENDO_BNTX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_bntx { Abstractformat format; } xx_nintendo_bntx;
XXFC_API void xx_nintendo_bntx_init(xx_nintendo_bntx *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_bntx *xx_nintendo_bntx_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_bntx_destroy(xx_nintendo_bntx *);
XXFC_API void xx_nintendo_bntx_free(xx_nintendo_bntx *);
XXFC_API bool xx_nintendo_bntx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_bntx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_bntx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_bntx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_bntx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_bntx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_bntx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
