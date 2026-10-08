/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/lanl/LaGriT/master/src/readgmv_binary.f
 * LANL GMV unstructured mesh subset: ASCII and little-endian IEEE32 binary nodes, fixed-arity cells, counted material/scalar variables and cycle/time records; binary additionally validates velocity, flags and polygon records with 2 to64 finite points, including LaGriT line exports. Complete local cardinalities and terminal endgmv are checked. Original typed mesh/data sections exported; structured/general-polyhedron/unknown extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_GMV_MESH_H
#define XX_GMV_MESH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gmv_mesh {Abstractformat format;} xx_gmv_mesh;
XXFC_API void xx_gmv_mesh_init(xx_gmv_mesh *,xx_io_device *,int64_t);
XXFC_API xx_gmv_mesh *xx_gmv_mesh_create(xx_io_device *,int64_t);
XXFC_API void xx_gmv_mesh_destroy(xx_gmv_mesh *);
XXFC_API void xx_gmv_mesh_free(xx_gmv_mesh *);
XXFC_API bool xx_gmv_mesh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gmv_mesh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gmv_mesh_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gmv_mesh_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gmv_mesh_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
