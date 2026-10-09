/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.esri.com/library/whitepapers/pdfs/shapefile.pdf */
#ifndef XX_ESRI_SHP_H
#define XX_ESRI_SHP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_esri_shp { Abstractformat format; } xx_esri_shp;
XXFC_API void xx_esri_shp_init(xx_esri_shp *,xx_io_device *,int64_t);
XXFC_API xx_esri_shp *xx_esri_shp_create(xx_io_device *,int64_t);
XXFC_API void xx_esri_shp_destroy(xx_esri_shp *);
XXFC_API void xx_esri_shp_free(xx_esri_shp *);
XXFC_API bool xx_esri_shp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_esri_shp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_esri_shp_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_esri_shp_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_esri_shp_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_esri_shp_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_esri_shp_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
