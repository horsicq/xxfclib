/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.csc.kth.se/~weinkauf/notes/amiramesh.html
 * AmiraMesh2.1 BINARY-LITTLE-ENDIAN uniform float lattice: complete dimensions/bounding-box/coordinate-type/field declaration and exact finite interleaved scalar/vector payload with optional single LF or CRLF terminator. Original ASCII descriptor and typed lattice planes exported; compressed/ASCII/unstructured/multiple-field extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_AMIRA_MESH_H
#define XX_AMIRA_MESH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amira_mesh {Abstractformat format;} xx_amira_mesh;
XXFC_API void xx_amira_mesh_init(xx_amira_mesh *,xx_io_device *,int64_t);
XXFC_API xx_amira_mesh *xx_amira_mesh_create(xx_io_device *,int64_t);
XXFC_API void xx_amira_mesh_destroy(xx_amira_mesh *);
XXFC_API void xx_amira_mesh_free(xx_amira_mesh *);
XXFC_API bool xx_amira_mesh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amira_mesh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_amira_mesh_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_amira_mesh_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_amira_mesh_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
