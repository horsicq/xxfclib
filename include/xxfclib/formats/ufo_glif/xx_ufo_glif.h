/* SPDX-License-Identifier: MIT
 * Primary reference: https://unifiedfontobject.org/versions/ufo3/glyphs/glif/
 * UFO GLIF2 standalone glyphs: complete bounded UTF8 XML and glyph metrics/Unicode/outline contour/point/component/anchor grammar; checked point topology and unique identifiers. Original descriptor and typed outline records exported; component base names retained without external loading; arbitrary lib/images/extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_UFO_GLIF_H
#define XX_UFO_GLIF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ufo_glif {Abstractformat format;} xx_ufo_glif;
XXFC_API void xx_ufo_glif_init(xx_ufo_glif *,xx_io_device *,int64_t);
XXFC_API xx_ufo_glif *xx_ufo_glif_create(xx_io_device *,int64_t);
XXFC_API void xx_ufo_glif_destroy(xx_ufo_glif *);
XXFC_API void xx_ufo_glif_free(xx_ufo_glif *);
XXFC_API bool xx_ufo_glif_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ufo_glif_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ufo_glif_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ufo_glif_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ufo_glif_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ufo_glif_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ufo_glif_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
