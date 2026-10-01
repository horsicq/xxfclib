/* SPDX-License-Identifier: MIT
 * Primary reference: https://wias-berlin.de/software/tetgen/1.5/doc/manual/manual006.html
 * TetGen standalone node/element sections: exact counted dimensions/attribute/marker columns, unique nonnegative IDs, finite coordinates/attributes and distinct bounded connectivity. Original descriptor and row records exported; node sidecars are not loaded and element references are retained, not resolved externally.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_TETGEN_MESH_H
#define XX_TETGEN_MESH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tetgen_mesh {Abstractformat format;} xx_tetgen_mesh;
XXFC_API void xx_tetgen_mesh_init(xx_tetgen_mesh *,xx_io_device *,int64_t);
XXFC_API xx_tetgen_mesh *xx_tetgen_mesh_create(xx_io_device *,int64_t);
XXFC_API void xx_tetgen_mesh_destroy(xx_tetgen_mesh *);
XXFC_API void xx_tetgen_mesh_free(xx_tetgen_mesh *);
XXFC_API bool xx_tetgen_mesh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tetgen_mesh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
