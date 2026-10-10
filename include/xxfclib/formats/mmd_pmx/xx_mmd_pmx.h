/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/MMD-Blender/blender_mmd_tools/main/mmd_tools/core/pmx/__init__.py
 * PMX2.0 model containers: complete bounded typed vertices/skinning/faces/textures/materials/bones/morphs/display/rigid-body/joint arrays with valid local indexes.
 * Original model sections exported; no external asset loading, simulation or rendering. Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_MMD_PMX_H
#define XX_MMD_PMX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mmd_pmx {
    Abstractformat format;
} xx_mmd_pmx;
XXFC_API void xx_mmd_pmx_init(xx_mmd_pmx *, xx_io_device *, int64_t);
XXFC_API xx_mmd_pmx *xx_mmd_pmx_create(xx_io_device *, int64_t);
XXFC_API void xx_mmd_pmx_destroy(xx_mmd_pmx *);
XXFC_API void xx_mmd_pmx_free(xx_mmd_pmx *);
XXFC_API bool xx_mmd_pmx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_mmd_pmx_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mmd_pmx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mmd_pmx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mmd_pmx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mmd_pmx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mmd_pmx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
