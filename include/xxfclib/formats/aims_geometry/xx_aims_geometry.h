/* SPDX-License-Identifier: MIT
 * Wire specification: https://fhi-aims.org/uploads/documents/FHI-aims.250320.pdf */
#ifndef XX_AIMS_GEOMETRY_H
#define XX_AIMS_GEOMETRY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_aims_geometry { Abstractformat format; } xx_aims_geometry;
XXFC_API void xx_aims_geometry_init(xx_aims_geometry *,xx_io_device *,int64_t);
XXFC_API xx_aims_geometry *xx_aims_geometry_create(xx_io_device *,int64_t);
XXFC_API void xx_aims_geometry_destroy(xx_aims_geometry *);
XXFC_API void xx_aims_geometry_free(xx_aims_geometry *);
XXFC_API bool xx_aims_geometry_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_aims_geometry_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_aims_geometry_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_aims_geometry_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_aims_geometry_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
