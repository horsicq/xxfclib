/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.ogc.org/standards/sfa/
 * OGC2D WKT POINT/LINESTRING/POLYGON/MULTIPOINT/MULTILINESTRING/MULTIPOLYGON and bounded nested GEOMETRYCOLLECTION: complete finite coordinate grammar, minimum vertex
 * counts, closed nondegenerate rings. Nonempty ASCII geometries only. Original typed geometry components exported; dimensional/SRID extensions and topology evaluation
 * declined. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_OGC_WKT_H
#define XX_OGC_WKT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ogc_wkt {
    Abstractformat format;
} xx_ogc_wkt;
XXFC_API void xx_ogc_wkt_init(xx_ogc_wkt *, xx_io_device *, int64_t);
XXFC_API xx_ogc_wkt *xx_ogc_wkt_create(xx_io_device *, int64_t);
XXFC_API void xx_ogc_wkt_destroy(xx_ogc_wkt *);
XXFC_API void xx_ogc_wkt_free(xx_ogc_wkt *);
XXFC_API bool xx_ogc_wkt_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ogc_wkt_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ogc_wkt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ogc_wkt_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ogc_wkt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ogc_wkt_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ogc_wkt_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
