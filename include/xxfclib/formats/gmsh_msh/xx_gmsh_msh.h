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
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gmsh_msh_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gmsh_msh_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gmsh_msh_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gmsh_msh_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gmsh_msh_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
