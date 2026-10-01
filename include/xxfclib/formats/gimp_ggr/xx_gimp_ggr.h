/* SPDX-License-Identifier: MIT
 * Primary reference: https://developer.gimp.org/core/standards/ggr/
 * GIMP GGR complete ordered color segments spanning0..1 with finite RGBA values and typed blend/color/endpoint enums. Original segments exported; interpolation/rendering unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_GIMP_GGR_H
#define XX_GIMP_GGR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gimp_ggr {Abstractformat format;} xx_gimp_ggr;
XXFC_API void xx_gimp_ggr_init(xx_gimp_ggr *,xx_io_device *,int64_t);
XXFC_API xx_gimp_ggr *xx_gimp_ggr_create(xx_io_device *,int64_t);
XXFC_API void xx_gimp_ggr_destroy(xx_gimp_ggr *);
XXFC_API void xx_gimp_ggr_free(xx_gimp_ggr *);
XXFC_API bool xx_gimp_ggr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gimp_ggr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
