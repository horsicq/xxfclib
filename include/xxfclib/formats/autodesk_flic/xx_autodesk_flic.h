/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/flic.c
 * Autodesk FLI/FLC8-bit indexed animations with complete128-byte header, frame counts and bounded palette/BRUN/LC/SS2/COPY/BLACK image chunk grammar. Original header, standard bounded opaque PREFIX/PSTAMP metadata and encoded frames, including an optional ring frame, are exported; non8-bit depths, unknown chunks and rendering are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_AUTODESK_FLIC_H
#define XX_AUTODESK_FLIC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_autodesk_flic { Abstractformat format; } xx_autodesk_flic;
XXFC_API void xx_autodesk_flic_init(xx_autodesk_flic *,xx_io_device *,int64_t);
XXFC_API xx_autodesk_flic *xx_autodesk_flic_create(xx_io_device *,int64_t);
XXFC_API void xx_autodesk_flic_destroy(xx_autodesk_flic *);
XXFC_API void xx_autodesk_flic_free(xx_autodesk_flic *);
XXFC_API bool xx_autodesk_flic_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_autodesk_flic_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_autodesk_flic_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_autodesk_flic_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_autodesk_flic_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
