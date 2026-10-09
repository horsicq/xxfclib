/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/opencv/opencv/4.x/modules/imgcodecs/src/grfmt_pfm.cpp
 * PF/Pf floating-point images with bounded complete dimension/scale grammar, exact finite float raster in declared endian order and no trailing bytes. Original encoded float raster exported; display conversion and nonfinite values are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_NETPBM_PFM_H
#define XX_NETPBM_PFM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_netpbm_pfm { Abstractformat format; } xx_netpbm_pfm;
XXFC_API void xx_netpbm_pfm_init(xx_netpbm_pfm *,xx_io_device *,int64_t);
XXFC_API xx_netpbm_pfm *xx_netpbm_pfm_create(xx_io_device *,int64_t);
XXFC_API void xx_netpbm_pfm_destroy(xx_netpbm_pfm *);
XXFC_API void xx_netpbm_pfm_free(xx_netpbm_pfm *);
XXFC_API bool xx_netpbm_pfm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_netpbm_pfm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_netpbm_pfm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_netpbm_pfm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_netpbm_pfm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_netpbm_pfm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_netpbm_pfm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
