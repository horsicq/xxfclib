/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://bellard.org/bpg/bpg_spec.txt
 * BPG still images with complete canonical variable-length dimensions, extension TLVs, reduced HEVC headers and bounded NAL framing/layer identities and PPS/SPS identifiers. Entropy-coded HEVC slices remain opaque; declared zero picture length consumes physical EOF and cannot prove slice completeness. Original encoded headers/picture data exported; animation and HEVC pixel decoding unsupported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_BPG_IMAGE_H
#define XX_BPG_IMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bpg_image {Abstractformat format;} xx_bpg_image;
XXFC_API void xx_bpg_image_init(xx_bpg_image *,xx_io_device *,int64_t);
XXFC_API xx_bpg_image *xx_bpg_image_create(xx_io_device *,int64_t);
XXFC_API void xx_bpg_image_destroy(xx_bpg_image *);
XXFC_API void xx_bpg_image_free(xx_bpg_image *);
XXFC_API bool xx_bpg_image_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bpg_image_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_bpg_image_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_bpg_image_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_bpg_image_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
