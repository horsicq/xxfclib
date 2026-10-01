/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/AcademySoftwareFoundation/OpenImageIO/main/src/softimage.imageio/softimageinput.cpp
 * Softimage PIC1.0/1.6 8/16-bit RGB/RGBA scanline packet descriptors with complete stored/pure-RLE/mixed-RLE channel packet runs and exact image extent. Original header and encoded scanlines exported; unsupported fields/channel combinations and rendering refused.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_SOFTIMAGE_PIC_H
#define XX_SOFTIMAGE_PIC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_softimage_pic {Abstractformat format;} xx_softimage_pic;
XXFC_API void xx_softimage_pic_init(xx_softimage_pic *,xx_io_device *,int64_t);
XXFC_API xx_softimage_pic *xx_softimage_pic_create(xx_io_device *,int64_t);
XXFC_API void xx_softimage_pic_destroy(xx_softimage_pic *);
XXFC_API void xx_softimage_pic_free(xx_softimage_pic *);
XXFC_API bool xx_softimage_pic_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_softimage_pic_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
