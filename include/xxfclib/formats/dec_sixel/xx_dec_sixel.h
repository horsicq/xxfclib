/* SPDX-License-Identifier: MIT
 * Primary reference: https://vt100.net/docs/vt3xx-gp/chapter14.html
 * DEC SIXEL RGB subset: complete DCS/ST framing, declared bounded raster, checked RGB color registers/repeats/carriage returns/newlines and pixel extents. Original descriptor/color program plus RGB raster exported using literal declared dimensions and black opaque background for unpainted pixels; no aspect-ratio resampling. Transparent backgrounds, HLS, animation and other terminal controls declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_DEC_SIXEL_H
#define XX_DEC_SIXEL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dec_sixel {Abstractformat format;} xx_dec_sixel;
XXFC_API void xx_dec_sixel_init(xx_dec_sixel *,xx_io_device *,int64_t);
XXFC_API xx_dec_sixel *xx_dec_sixel_create(xx_io_device *,int64_t);
XXFC_API void xx_dec_sixel_destroy(xx_dec_sixel *);
XXFC_API void xx_dec_sixel_free(xx_dec_sixel *);
XXFC_API bool xx_dec_sixel_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dec_sixel_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
