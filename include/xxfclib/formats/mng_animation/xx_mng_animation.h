/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: http://www.libpng.org/pub/mng/spec/mng-1.0-20010209-pdg.html
 * MNG1 simple complete PNG frame sequences: CRC32-checked MHDR/IHDR/PLTE/tRNS/IDAT/IEND/MEND chunks, required dimensions, typed ancillary records and complete frame ordering. Encoded IDAT is framed with an RFC1950 header but DEFLATE/Adler integrity and raster samples are not decoded. Original encoded chunks exported; JNG/delta/object control/loops and pixel decoding unsupported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_MNG_ANIMATION_H
#define XX_MNG_ANIMATION_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mng_animation {Abstractformat format;} xx_mng_animation;
XXFC_API void xx_mng_animation_init(xx_mng_animation *,xx_io_device *,int64_t);
XXFC_API xx_mng_animation *xx_mng_animation_create(xx_io_device *,int64_t);
XXFC_API void xx_mng_animation_destroy(xx_mng_animation *);
XXFC_API void xx_mng_animation_free(xx_mng_animation *);
XXFC_API bool xx_mng_animation_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mng_animation_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mng_animation_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mng_animation_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mng_animation_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
