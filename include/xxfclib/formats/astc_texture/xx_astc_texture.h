/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ARM-software/astc-encoder/main/Docs/FileFormat.md
 * ASTC 2D containers with standard block footprints, positive 24-bit dimensions and an exact complete encoded block array. Blocks remain encoded; 3D textures and texture decoding are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_ASTC_TEXTURE_H
#define XX_ASTC_TEXTURE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_astc_texture { Abstractformat format; } xx_astc_texture;
XXFC_API void xx_astc_texture_init(xx_astc_texture *,xx_io_device *,int64_t);
XXFC_API xx_astc_texture *xx_astc_texture_create(xx_io_device *,int64_t);
XXFC_API void xx_astc_texture_destroy(xx_astc_texture *);
XXFC_API void xx_astc_texture_free(xx_astc_texture *);
XXFC_API bool xx_astc_texture_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_astc_texture_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
