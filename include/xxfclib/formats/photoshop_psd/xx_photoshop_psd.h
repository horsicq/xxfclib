/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout references: https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
 * Independently implemented; extracts stored encoded components without media decoding.
 */
#ifndef XX_PHOTOSHOP_PSD_H
#define XX_PHOTOSHOP_PSD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_photoshop_psd { Abstractformat format; } xx_photoshop_psd;
XXFC_API void xx_photoshop_psd_init(xx_photoshop_psd *,xx_io_device *,int64_t);
XXFC_API xx_photoshop_psd *xx_photoshop_psd_create(xx_io_device *,int64_t);
XXFC_API void xx_photoshop_psd_destroy(xx_photoshop_psd *);
XXFC_API void xx_photoshop_psd_free(xx_photoshop_psd *);
XXFC_API bool xx_photoshop_psd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_photoshop_psd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_photoshop_psd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_photoshop_psd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_photoshop_psd_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
