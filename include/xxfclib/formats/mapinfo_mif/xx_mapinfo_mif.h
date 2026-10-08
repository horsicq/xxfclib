/* SPDX-License-Identifier: MIT
 * Primary reference: https://docs.safe.com/fme/2016.0/html/FME_Desktop_Documentation/FME_ReadersWriters/mif/mif.htm
 * MapInfo MIF vector geometry subset: complete version/charset/columns descriptor and bounded typed point/line/polyline/region/rectangle/ellipse geometries with finite coordinates, checked counts/closed rings and typed pen/brush/symbol metadata. Original descriptor and geometry components exported; external MID attributes/complex projection/collection/text extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_MAPINFO_MIF_H
#define XX_MAPINFO_MIF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mapinfo_mif {Abstractformat format;} xx_mapinfo_mif;
XXFC_API void xx_mapinfo_mif_init(xx_mapinfo_mif *,xx_io_device *,int64_t);
XXFC_API xx_mapinfo_mif *xx_mapinfo_mif_create(xx_io_device *,int64_t);
XXFC_API void xx_mapinfo_mif_destroy(xx_mapinfo_mif *);
XXFC_API void xx_mapinfo_mif_free(xx_mapinfo_mif *);
XXFC_API bool xx_mapinfo_mif_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mapinfo_mif_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mapinfo_mif_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mapinfo_mif_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mapinfo_mif_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
