/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ValveSoftware/halflife/master/engine/studio.h
 * GoldSrc studio model IDST version10 texture-only companions, up to100 indexed textures and bounded skin families. Validates texture/name/palette/skin-table ranges and indices. Exports indexed planes, RGB palettes and skin mappings; geometry/animation/sequence groups, external textures and rendering unsupported.
 */
#ifndef XX_VALVE_STUDIO_MDL_H
#define XX_VALVE_STUDIO_MDL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_valve_studio_mdl { Abstractformat format; } xx_valve_studio_mdl;
XXFC_API void xx_valve_studio_mdl_init(xx_valve_studio_mdl *,xx_io_device *,int64_t);
XXFC_API xx_valve_studio_mdl *xx_valve_studio_mdl_create(xx_io_device *,int64_t);
XXFC_API void xx_valve_studio_mdl_destroy(xx_valve_studio_mdl *);
XXFC_API void xx_valve_studio_mdl_free(xx_valve_studio_mdl *);
XXFC_API bool xx_valve_studio_mdl_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_valve_studio_mdl_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
