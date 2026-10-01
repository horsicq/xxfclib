/* SPDX-License-Identifier: MIT
 * Primary reference: https://ansyshelp.ansys.com/public/Views/Secured/corp/v261/en/pdf/Ansys_EnSight_User_Manual.pdf
 * EnSight Gold C-binary little-endian unstructured geometry: complete header, explicit part IDs, unique nonnegative signed32-bit given node/element IDs, finite coordinate tables and typed fixed-arity connectivity with bounded 1-based local indexes. Original descriptor/part coordinate and connectivity tables exported; Fortran binary/ASCII/structured/polygon/time blocks declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_ENSIGHT_GOLD_GEOMETRY_H
#define XX_ENSIGHT_GOLD_GEOMETRY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ensight_gold_geometry {Abstractformat format;} xx_ensight_gold_geometry;
XXFC_API void xx_ensight_gold_geometry_init(xx_ensight_gold_geometry *,xx_io_device *,int64_t);
XXFC_API xx_ensight_gold_geometry *xx_ensight_gold_geometry_create(xx_io_device *,int64_t);
XXFC_API void xx_ensight_gold_geometry_destroy(xx_ensight_gold_geometry *);
XXFC_API void xx_ensight_gold_geometry_free(xx_ensight_gold_geometry *);
XXFC_API bool xx_ensight_gold_geometry_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ensight_gold_geometry_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
