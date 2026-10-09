/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FNA-XNA/FNA/master/src/Content/ContentReader.cs
 * Uncompressed XNB5, one version0 reader and no shared resources: Texture2DReader(Color/Bgr565/Bgra5551/Bgra4444), StringReader, ByteReader, Int32Reader or SingleReader. Parses canonical7-bit lengths, exact root reader and mip dimensions/byte sizes. Up to16 mips,8192 dimensions,16million pixels,64MiB file. Exports encoded mip/value payloads; LZX/LZ4, complex/custom/shared objects, texture conversion and rendering unsupported.
 */
#ifndef XX_XNA_XNB_H
#define XX_XNA_XNB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xna_xnb { Abstractformat format; } xx_xna_xnb;
XXFC_API void xx_xna_xnb_init(xx_xna_xnb *,xx_io_device *,int64_t);
XXFC_API xx_xna_xnb *xx_xna_xnb_create(xx_io_device *,int64_t);
XXFC_API void xx_xna_xnb_destroy(xx_xna_xnb *);
XXFC_API void xx_xna_xnb_free(xx_xna_xnb *);
XXFC_API bool xx_xna_xnb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_xna_xnb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_xna_xnb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_xna_xnb_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_xna_xnb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_xna_xnb_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_xna_xnb_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
