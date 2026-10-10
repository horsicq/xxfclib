/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/TorqueGameEngines/Torque3D/development/Engine/source/ts/tsShape.cpp
 * Torque DTS24 static node-only shapes with1-256 nodes, one subshape, default transforms/names and no meshes/objects/materials/sequences. Parses all three split scalar
 * buffers and17 synchronized guards, hierarchy and bounded name records. Exports nodes/default transforms/names; geometry, animation, newer encrypted versions and
 * rendering unsupported.
 */
#ifndef XX_TORQUE_DTS_H
#define XX_TORQUE_DTS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_torque_dts {
    Abstractformat format;
} xx_torque_dts;
XXFC_API void xx_torque_dts_init(xx_torque_dts *, xx_io_device *, int64_t);
XXFC_API xx_torque_dts *xx_torque_dts_create(xx_io_device *, int64_t);
XXFC_API void xx_torque_dts_destroy(xx_torque_dts *);
XXFC_API void xx_torque_dts_free(xx_torque_dts *);
XXFC_API bool xx_torque_dts_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_torque_dts_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_torque_dts_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_torque_dts_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_torque_dts_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_torque_dts_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_torque_dts_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
