/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/ASE/ASEParser.cpp
 * Autodesk ASE200 static ASCII triangle meshes: complete typed scene, Standard materials, node transforms, ordered unique vertex/face IDs, finite coordinates and local index/material-reference checks. Original encoded sections exported. Nonzero animation times, UV/normal/color lists, external maps, hierarchy nodes and other material/node extensions declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_AUTODESK_ASE_H
#define XX_AUTODESK_ASE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_autodesk_ase {Abstractformat format;} xx_autodesk_ase;
XXFC_API void xx_autodesk_ase_init(xx_autodesk_ase *,xx_io_device *,int64_t);
XXFC_API xx_autodesk_ase *xx_autodesk_ase_create(xx_io_device *,int64_t);
XXFC_API void xx_autodesk_ase_destroy(xx_autodesk_ase *);
XXFC_API void xx_autodesk_ase_free(xx_autodesk_ase *);
XXFC_API bool xx_autodesk_ase_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_autodesk_ase_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
