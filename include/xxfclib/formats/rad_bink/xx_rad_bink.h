/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/bink.c
 * Bink1 revisions b/f/g/h/i, complete dimensions/rate, bounded audio descriptors, increasing physical frame index and per-frame audio/video extents. Original header/index and complete encoded frames are exported; Bink2/SMUSH wrappers, unknown revisions and codec decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_RAD_BINK_H
#define XX_RAD_BINK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_rad_bink { Abstractformat format; } xx_rad_bink;
XXFC_API void xx_rad_bink_init(xx_rad_bink *,xx_io_device *,int64_t);
XXFC_API xx_rad_bink *xx_rad_bink_create(xx_io_device *,int64_t);
XXFC_API void xx_rad_bink_destroy(xx_rad_bink *);
XXFC_API void xx_rad_bink_free(xx_rad_bink *);
XXFC_API bool xx_rad_bink_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_rad_bink_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_rad_bink_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_rad_bink_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_rad_bink_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
