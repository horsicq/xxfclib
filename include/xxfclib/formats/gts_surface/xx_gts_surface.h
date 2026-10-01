/* SPDX-License-Identifier: MIT
 * Primary reference: https://gts.sourceforge.net/reference/gts-surfaces.html
 * GNU GTS ASCII base classes: complete declared vertex/edge/triangle tables, finite coordinates and valid distinct local edge/vertex references forming each triangle. Original typed tables exported; custom per-object attributes and progressive surfaces declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_GTS_SURFACE_H
#define XX_GTS_SURFACE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gts_surface {Abstractformat format;} xx_gts_surface;
XXFC_API void xx_gts_surface_init(xx_gts_surface *,xx_io_device *,int64_t);
XXFC_API xx_gts_surface *xx_gts_surface_create(xx_io_device *,int64_t);
XXFC_API void xx_gts_surface_destroy(xx_gts_surface *);
XXFC_API void xx_gts_surface_free(xx_gts_surface *);
XXFC_API bool xx_gts_surface_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gts_surface_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
