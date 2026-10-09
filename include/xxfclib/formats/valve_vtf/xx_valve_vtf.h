/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/public/vtf/vtf.h
 * Valve VTF 7.0–7.2 encoded thumbnails and mip/frame/face surfaces (RGBA/RGB/BGR/BGRA, DXT1/3/5). Resource-table 7.3+ and Xbox variants are rejected; no pixel decoding.
 */
#ifndef XX_VALVE_VTF_H
#define XX_VALVE_VTF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_valve_vtf { Abstractformat format; } xx_valve_vtf;
XXFC_API void xx_valve_vtf_init(xx_valve_vtf *,xx_io_device *,int64_t);
XXFC_API xx_valve_vtf *xx_valve_vtf_create(xx_io_device *,int64_t);
XXFC_API void xx_valve_vtf_destroy(xx_valve_vtf *);
XXFC_API void xx_valve_vtf_free(xx_valve_vtf *);
XXFC_API bool xx_valve_vtf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_valve_vtf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_valve_vtf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_valve_vtf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_valve_vtf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_valve_vtf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_valve_vtf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
