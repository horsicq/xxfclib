/* SPDX-License-Identifier: MIT
 * Primary reference: https://doc.esri.com/en/arcgis-pro/latest/tool-reference/conversion/raster-to-ascii.html
 * Esri ASCII grid: complete case-insensitive NCOLS/NROWS, paired corner or center georeference, positive cell size, optional finite nodata, exact finite sample count; fixed NCOLS/NROWS then XY/cellsize header order. Original descriptor and raster rows exported; NaN/Inf and nonuniform DX/DY extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_ESRI_ASCII_GRID_H
#define XX_ESRI_ASCII_GRID_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_esri_ascii_grid {Abstractformat format;} xx_esri_ascii_grid;
XXFC_API void xx_esri_ascii_grid_init(xx_esri_ascii_grid *,xx_io_device *,int64_t);
XXFC_API xx_esri_ascii_grid *xx_esri_ascii_grid_create(xx_io_device *,int64_t);
XXFC_API void xx_esri_ascii_grid_destroy(xx_esri_ascii_grid *);
XXFC_API void xx_esri_ascii_grid_free(xx_esri_ascii_grid *);
XXFC_API bool xx_esri_ascii_grid_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_esri_ascii_grid_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_esri_ascii_grid_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_esri_ascii_grid_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_esri_ascii_grid_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_esri_ascii_grid_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_esri_ascii_grid_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
