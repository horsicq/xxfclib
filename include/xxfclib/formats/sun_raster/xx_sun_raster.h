/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://www.cs.cmu.edu/~maxwell/misc/vascHelpPages/sunRasterFormat.html, https://gitlab.gnome.org/GNOME/gimp/-/blob/master/plug-ins/common/file-sunras.c
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_SUN_RASTER_H
#define XX_SUN_RASTER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sun_raster { Abstractformat format; } xx_sun_raster;
XXFC_API void xx_sun_raster_init(xx_sun_raster *,xx_io_device *,int64_t);
XXFC_API xx_sun_raster *xx_sun_raster_create(xx_io_device *,int64_t);
XXFC_API void xx_sun_raster_destroy(xx_sun_raster *);
XXFC_API void xx_sun_raster_free(xx_sun_raster *);
XXFC_API bool xx_sun_raster_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sun_raster_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sun_raster_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sun_raster_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sun_raster_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sun_raster_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sun_raster_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
