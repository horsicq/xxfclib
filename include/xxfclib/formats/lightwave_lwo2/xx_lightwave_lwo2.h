/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/LWO/LWOFileData.h
 * LWO2 single-layer polygon meshes with LAYR,PNTS,POLS FACE,TAGS and optional PTAG SURF. Checks IFF padding, unique section framing, finite geometry, VX point/polygon references and tag indices. Up to65536 points/polygons/tags. Exports each encoded chunk; surfaces, vertex maps, envelopes, other polygon types and rendering unsupported.
 */
#ifndef XX_LIGHTWAVE_LWO2_H
#define XX_LIGHTWAVE_LWO2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lightwave_lwo2 { Abstractformat format; } xx_lightwave_lwo2;
XXFC_API void xx_lightwave_lwo2_init(xx_lightwave_lwo2 *,xx_io_device *,int64_t);
XXFC_API xx_lightwave_lwo2 *xx_lightwave_lwo2_create(xx_io_device *,int64_t);
XXFC_API void xx_lightwave_lwo2_destroy(xx_lightwave_lwo2 *);
XXFC_API void xx_lightwave_lwo2_free(xx_lightwave_lwo2 *);
XXFC_API bool xx_lightwave_lwo2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lightwave_lwo2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lightwave_lwo2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lightwave_lwo2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lightwave_lwo2_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
