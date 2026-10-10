/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/KiCad/kicad-source-mirror/master/pcbnew/exporters/excellon_writer.cpp
 * Excellon drill subset: complete M48 header, INCH/METRIC zero-suppression and tool diameters, resolved tool selections and six-digit integer drill positions
 * (INCH2:4/METRIC3:3, leading or trailing suppression) terminated by M30. Original encoded tools/positions exported; routing/repeat/machine-control extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_EXCELLON_DRILL_H
#define XX_EXCELLON_DRILL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_excellon_drill {
    Abstractformat format;
} xx_excellon_drill;
XXFC_API void xx_excellon_drill_init(xx_excellon_drill *, xx_io_device *, int64_t);
XXFC_API xx_excellon_drill *xx_excellon_drill_create(xx_io_device *, int64_t);
XXFC_API void xx_excellon_drill_destroy(xx_excellon_drill *);
XXFC_API void xx_excellon_drill_free(xx_excellon_drill *);
XXFC_API bool xx_excellon_drill_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_excellon_drill_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_excellon_drill_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_excellon_drill_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_excellon_drill_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_excellon_drill_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_excellon_drill_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
