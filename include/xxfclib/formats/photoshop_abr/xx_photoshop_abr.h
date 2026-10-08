/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/GNOME/gimp/master/app/core/gimpbrush-load.c
 * ABR1/2 sampled8-bit brushes only: complete count/record framing, bounded UTF16 names/geometry, stored or fully validated row PackBits. Original brush metadata and encoded bitmap payloads exported; computed brushes and ABR6+ unsupported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#ifndef XX_PHOTOSHOP_ABR_H
#define XX_PHOTOSHOP_ABR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_photoshop_abr {Abstractformat format;} xx_photoshop_abr;
XXFC_API void xx_photoshop_abr_init(xx_photoshop_abr *,xx_io_device *,int64_t);
XXFC_API xx_photoshop_abr *xx_photoshop_abr_create(xx_io_device *,int64_t);
XXFC_API void xx_photoshop_abr_destroy(xx_photoshop_abr *);
XXFC_API void xx_photoshop_abr_free(xx_photoshop_abr *);
XXFC_API bool xx_photoshop_abr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_photoshop_abr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_photoshop_abr_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_photoshop_abr_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_photoshop_abr_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
