/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/viff.c
 * Khoros VIFFv1 images: complete1024-byte typed descriptor, implicit finite geometry, counted8-bit/16-bit/32-bit/finite-float planes and bounded byte maps with exact EOF. Original descriptor/maps/planes exported; explicit locations, encoding/unknown image extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_KHOROS_VIFF_H
#define XX_KHOROS_VIFF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_khoros_viff {Abstractformat format;} xx_khoros_viff;
XXFC_API void xx_khoros_viff_init(xx_khoros_viff *,xx_io_device *,int64_t);
XXFC_API xx_khoros_viff *xx_khoros_viff_create(xx_io_device *,int64_t);
XXFC_API void xx_khoros_viff_destroy(xx_khoros_viff *);
XXFC_API void xx_khoros_viff_free(xx_khoros_viff *);
XXFC_API bool xx_khoros_viff_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_khoros_viff_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
