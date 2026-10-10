/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/aqsis/aqsis/master/libs/riutil/ribparser.cpp
 * RenderMan ASCII static RIB subset: complete typed camera/display/transform/world/frame/attribute commands and Polygon/PointsPolygons/Sphere geometry, finite arrays,
 * local indexes and balanced scopes; geometry P precedes optional N/Cs/Os/st arrays. Original encoded commands exported; shaders retained only as typed metadata, never
 * loaded. Binary RIB, motion, archives/procedurals, custom parameters and rendering declined. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_RENDERMAN_RIB_H
#define XX_RENDERMAN_RIB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_renderman_rib {
    Abstractformat format;
} xx_renderman_rib;
XXFC_API void xx_renderman_rib_init(xx_renderman_rib *, xx_io_device *, int64_t);
XXFC_API xx_renderman_rib *xx_renderman_rib_create(xx_io_device *, int64_t);
XXFC_API void xx_renderman_rib_destroy(xx_renderman_rib *);
XXFC_API void xx_renderman_rib_free(xx_renderman_rib *);
XXFC_API bool xx_renderman_rib_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_renderman_rib_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_renderman_rib_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_renderman_rib_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_renderman_rib_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_renderman_rib_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_renderman_rib_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
