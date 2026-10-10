/* SPDX-License-Identifier: MIT
 * Primary reference: https://gdal.org/en/stable/drivers/raster/gxf.html
 * GXF3 uncompressed numeric grids: complete unique counted header fields, bounded POINTS/ROWS, finite georeference/nodata and exactly declared samples; complete metadata
 * sections and optional EOF. Original header and sample rows exported. Compressed GTYPE, transforms/projection and unknown header extensions declined. Bounded32MiB
 * input,4096 components and bounded work.
 */
#ifndef XX_GXF_GRID_H
#define XX_GXF_GRID_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gxf_grid {
    Abstractformat format;
} xx_gxf_grid;
XXFC_API void xx_gxf_grid_init(xx_gxf_grid *, xx_io_device *, int64_t);
XXFC_API xx_gxf_grid *xx_gxf_grid_create(xx_io_device *, int64_t);
XXFC_API void xx_gxf_grid_destroy(xx_gxf_grid *);
XXFC_API void xx_gxf_grid_free(xx_gxf_grid *);
XXFC_API bool xx_gxf_grid_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_gxf_grid_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gxf_grid_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gxf_grid_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gxf_grid_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gxf_grid_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gxf_grid_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
