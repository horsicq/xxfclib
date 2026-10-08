/* SPDX-License-Identifier: MIT
 * Primary reference: https://gdal.org/en/stable/drivers/raster/dted.html
 * DTED levels0/1/2: complete UHL/DSI/ACC framing, exact declared columns/rows, consecutive per-column record identities, signed-magnitude elevation samples and verified additive record checksums. Original descriptor and encoded elevation columns exported; sparse/truncated/nonconformant variants declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_DTED_ELEVATION_H
#define XX_DTED_ELEVATION_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dted_elevation {Abstractformat format;} xx_dted_elevation;
XXFC_API void xx_dted_elevation_init(xx_dted_elevation *,xx_io_device *,int64_t);
XXFC_API xx_dted_elevation *xx_dted_elevation_create(xx_io_device *,int64_t);
XXFC_API void xx_dted_elevation_destroy(xx_dted_elevation *);
XXFC_API void xx_dted_elevation_free(xx_dted_elevation *);
XXFC_API bool xx_dted_elevation_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dted_elevation_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dted_elevation_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dted_elevation_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dted_elevation_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
