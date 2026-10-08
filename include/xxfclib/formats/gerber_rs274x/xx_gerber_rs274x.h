/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.ucamco.com/en/sales/downloads
 * Gerber RS274X linear subset: complete FS/MO and basic C/R/O/P aperture declarations, modal coordinate/tool state, G01 draw/flash and bounded closed G36/G37 regions, polarity plus typed X2 attributes and terminal M02. Original encoded commands exported. Aperture macros, arcs, transformations, blocks/repeats and rendering declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_GERBER_RS274X_H
#define XX_GERBER_RS274X_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gerber_rs274x {Abstractformat format;} xx_gerber_rs274x;
XXFC_API void xx_gerber_rs274x_init(xx_gerber_rs274x *,xx_io_device *,int64_t);
XXFC_API xx_gerber_rs274x *xx_gerber_rs274x_create(xx_io_device *,int64_t);
XXFC_API void xx_gerber_rs274x_destroy(xx_gerber_rs274x *);
XXFC_API void xx_gerber_rs274x_free(xx_gerber_rs274x *);
XXFC_API bool xx_gerber_rs274x_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gerber_rs274x_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gerber_rs274x_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gerber_rs274x_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gerber_rs274x_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
