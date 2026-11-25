/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/BinomialLLC/basis_universal/master/transcoder/basisu_file_headers.h
 * Basis header77 versions16-19, ETC1S/UASTC4x4 2D images with ordered mip slices and consistent ETC1S alpha-pair/UASTC alpha flags, complete disjoint
 * codebook/table/descriptor/slice spans and header/data CRC16 (plus UASTC slice CRC16). ETC1S decoded-slice CRCs remain encoded. Compression remains encoded; external
 * codebooks, extended data, HDR/XUASTC, other texture types and transcoding are unsupported. Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_BASIS_TEXTURE_H
#define XX_BASIS_TEXTURE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_basis_texture {
    Abstractformat format;
} xx_basis_texture;
XXFC_API void xx_basis_texture_init(xx_basis_texture *, xx_io_device *, int64_t);
XXFC_API xx_basis_texture *xx_basis_texture_create(xx_io_device *, int64_t);
XXFC_API void xx_basis_texture_destroy(xx_basis_texture *);
XXFC_API void xx_basis_texture_free(xx_basis_texture *);
XXFC_API bool xx_basis_texture_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_basis_texture_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_basis_texture_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_basis_texture_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_basis_texture_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_basis_texture_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_basis_texture_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
