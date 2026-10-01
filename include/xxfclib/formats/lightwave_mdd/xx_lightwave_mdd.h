/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/blender/blender-addons/main/io_shape_mdd/export_mdd.py
 * Big-endian MDD vertex caches with1-4096 frames,1-65536 points and at most1million frame-point triples. Validates exact complete size, finite strictly increasing nonnegative timestamps and finite XYZ arrays. Exports timestamps and one original position array per frame; deformation playback unsupported. Signatureless detection/search is offset-zero only.
 */
#ifndef XX_LIGHTWAVE_MDD_H
#define XX_LIGHTWAVE_MDD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lightwave_mdd { Abstractformat format; } xx_lightwave_mdd;
XXFC_API void xx_lightwave_mdd_init(xx_lightwave_mdd *,xx_io_device *,int64_t);
XXFC_API xx_lightwave_mdd *xx_lightwave_mdd_create(xx_io_device *,int64_t);
XXFC_API void xx_lightwave_mdd_destroy(xx_lightwave_mdd *);
XXFC_API void xx_lightwave_mdd_free(xx_lightwave_mdd *);
XXFC_API bool xx_lightwave_mdd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lightwave_mdd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
