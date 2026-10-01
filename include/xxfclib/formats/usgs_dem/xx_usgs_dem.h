/* SPDX-License-Identifier: MIT
 * Primary reference: https://pubs.usgs.gov/dug/0005/dug0005.pdf
 * USGS ASCII DEM: complete fixed1024-byte A descriptor and counted B elevation profiles/continuations, finite header/profile numbers, exact sample extents and matching elevation extrema. Optional LF/CRLF physical record separators accepted. Original descriptor/profile elevation records exported; C accuracy records, rotated grids and unsupported datums/projection/profile layouts declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_USGS_DEM_H
#define XX_USGS_DEM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_usgs_dem {Abstractformat format;} xx_usgs_dem;
XXFC_API void xx_usgs_dem_init(xx_usgs_dem *,xx_io_device *,int64_t);
XXFC_API xx_usgs_dem *xx_usgs_dem_create(xx_io_device *,int64_t);
XXFC_API void xx_usgs_dem_destroy(xx_usgs_dem *);
XXFC_API void xx_usgs_dem_free(xx_usgs_dem *);
XXFC_API bool xx_usgs_dem_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_usgs_dem_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
