/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/GNOME/gimp/master/plug-ins/common/file-psp.c
 * Native PSP3.0 RGB24/grayscale8 raster images with one color bitmap per layer: exact38-byte image,375-byte layer and12-byte channel descriptors, bounded rectangles/counts, stored or PSP RLE plane framing. Original encoded headers/channels exported; creator/color-table/alpha/user-mask blocks, other versions/compression and rendering declined. Primary original unavailable; independently specified wire controls only.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_PAINTSHOP_PSP_H
#define XX_PAINTSHOP_PSP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_paintshop_psp {Abstractformat format;} xx_paintshop_psp;
XXFC_API void xx_paintshop_psp_init(xx_paintshop_psp *,xx_io_device *,int64_t);
XXFC_API xx_paintshop_psp *xx_paintshop_psp_create(xx_io_device *,int64_t);
XXFC_API void xx_paintshop_psp_destroy(xx_paintshop_psp *);
XXFC_API void xx_paintshop_psp_free(xx_paintshop_psp *);
XXFC_API bool xx_paintshop_psp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_paintshop_psp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
