/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/1j01/anypalette.js/master/src/formats/PaintShopPro.coffee
 * JASC-PAL0100: exact declared RGB8 color rows and complete text framing. Original descriptor and palette rows exported; no rendering.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_JASC_PALETTE_H
#define XX_JASC_PALETTE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_jasc_palette {Abstractformat format;} xx_jasc_palette;
XXFC_API void xx_jasc_palette_init(xx_jasc_palette *,xx_io_device *,int64_t);
XXFC_API xx_jasc_palette *xx_jasc_palette_create(xx_io_device *,int64_t);
XXFC_API void xx_jasc_palette_destroy(xx_jasc_palette *);
XXFC_API void xx_jasc_palette_free(xx_jasc_palette *);
XXFC_API bool xx_jasc_palette_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_jasc_palette_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
