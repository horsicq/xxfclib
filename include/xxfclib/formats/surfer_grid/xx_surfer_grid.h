/* SPDX-License-Identifier: MIT
 * Primary reference: https://surferhelp.goldensoftware.com/topics/ascii_grid_file_format.htm
 * Golden Software Surfer DSAA ASCII grids: complete dimensions, increasing XY ranges, finite nodata/sample count and verified actual Z minimum/maximum (relative tolerance1e-12). Original descriptor and raster rows exported; binary/fault records declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_SURFER_GRID_H
#define XX_SURFER_GRID_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_surfer_grid {Abstractformat format;} xx_surfer_grid;
XXFC_API void xx_surfer_grid_init(xx_surfer_grid *,xx_io_device *,int64_t);
XXFC_API xx_surfer_grid *xx_surfer_grid_create(xx_io_device *,int64_t);
XXFC_API void xx_surfer_grid_destroy(xx_surfer_grid *);
XXFC_API void xx_surfer_grid_free(xx_surfer_grid *);
XXFC_API bool xx_surfer_grid_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_surfer_grid_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
