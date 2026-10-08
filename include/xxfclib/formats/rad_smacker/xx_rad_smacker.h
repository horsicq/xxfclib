/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/smacker.c
 * Smacker2/4 with complete Huffman-tree size descriptors, frame-size/type tables and bounded audio/palette/video packets. Original encoded trees and frames are exported; compressed codec/tree semantics remain encoded and unknown extensions are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_RAD_SMACKER_H
#define XX_RAD_SMACKER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_rad_smacker { Abstractformat format; } xx_rad_smacker;
XXFC_API void xx_rad_smacker_init(xx_rad_smacker *,xx_io_device *,int64_t);
XXFC_API xx_rad_smacker *xx_rad_smacker_create(xx_io_device *,int64_t);
XXFC_API void xx_rad_smacker_destroy(xx_rad_smacker *);
XXFC_API void xx_rad_smacker_free(xx_rad_smacker *);
XXFC_API bool xx_rad_smacker_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_rad_smacker_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_rad_smacker_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_rad_smacker_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_rad_smacker_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
