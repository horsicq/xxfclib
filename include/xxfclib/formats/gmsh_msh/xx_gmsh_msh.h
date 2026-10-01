/* SPDX-License-Identifier: MIT
 * Primary reference: https://gmsh.info/doc/texinfo/#MSH-file-format-version-2-_0028Legacy_0029
 * Gmsh MSH2.2 ASCII: exact section/count framing, unique node/element IDs, finite coordinates, complete supported element topology/tags and bounded physical names/data. Original mesh sections exported; binary/newer versions and unknown sections unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_GMSH_MSH_H
#define XX_GMSH_MSH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gmsh_msh {Abstractformat format;} xx_gmsh_msh;
XXFC_API void xx_gmsh_msh_init(xx_gmsh_msh *,xx_io_device *,int64_t);
XXFC_API xx_gmsh_msh *xx_gmsh_msh_create(xx_io_device *,int64_t);
XXFC_API void xx_gmsh_msh_destroy(xx_gmsh_msh *);
XXFC_API void xx_gmsh_msh_free(xx_gmsh_msh *);
XXFC_API bool xx_gmsh_msh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gmsh_msh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
