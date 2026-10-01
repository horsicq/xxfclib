/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/TorqueGameEngines/Torque3D/development/Engine/source/ts/tsShape.cpp
 * Torque DTS24 static node-only shapes with1-256 nodes, one subshape, default transforms/names and no meshes/objects/materials/sequences. Parses all three split scalar buffers and17 synchronized guards, hierarchy and bounded name records. Exports nodes/default transforms/names; geometry, animation, newer encrypted versions and rendering unsupported.
 */
#ifndef XX_TORQUE_DTS_H
#define XX_TORQUE_DTS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_torque_dts { Abstractformat format; } xx_torque_dts;
XXFC_API void xx_torque_dts_init(xx_torque_dts *,xx_io_device *,int64_t);
XXFC_API xx_torque_dts *xx_torque_dts_create(xx_io_device *,int64_t);
XXFC_API void xx_torque_dts_destroy(xx_torque_dts *);
XXFC_API void xx_torque_dts_free(xx_torque_dts *);
XXFC_API bool xx_torque_dts_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_torque_dts_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
