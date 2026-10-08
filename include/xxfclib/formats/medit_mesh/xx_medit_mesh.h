/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/LoicMarechal/libMeshb/master/README.md
 * INRIA MEDIT ASCII mesh v1/v2: complete dimension, finite vertex table, typed edge/triangle/quad/tetra/hex connectivity and reference labels, counted Corners and RequiredVertices records. Original typed sections exported; binary/solution/high-order and other auxiliary extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_MEDIT_MESH_H
#define XX_MEDIT_MESH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_medit_mesh {Abstractformat format;} xx_medit_mesh;
XXFC_API void xx_medit_mesh_init(xx_medit_mesh *,xx_io_device *,int64_t);
XXFC_API xx_medit_mesh *xx_medit_mesh_create(xx_io_device *,int64_t);
XXFC_API void xx_medit_mesh_destroy(xx_medit_mesh *);
XXFC_API void xx_medit_mesh_free(xx_medit_mesh *);
XXFC_API bool xx_medit_mesh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_medit_mesh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_medit_mesh_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_medit_mesh_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_medit_mesh_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
