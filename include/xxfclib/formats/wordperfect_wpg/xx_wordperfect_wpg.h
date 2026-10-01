/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/wpg.c
 * WordPerfect Graphics WPGv1 bitmap subset: complete version/header/counted start/palette/bitmap/end records, palette bounds and complete checked byte/row-copy RLE. Original records and decoded packed bitmaps exported; v2, embedded PostScript, vector/unknown records declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_WORDPERFECT_WPG_H
#define XX_WORDPERFECT_WPG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wordperfect_wpg {Abstractformat format;} xx_wordperfect_wpg;
XXFC_API void xx_wordperfect_wpg_init(xx_wordperfect_wpg *,xx_io_device *,int64_t);
XXFC_API xx_wordperfect_wpg *xx_wordperfect_wpg_create(xx_io_device *,int64_t);
XXFC_API void xx_wordperfect_wpg_destroy(xx_wordperfect_wpg *);
XXFC_API void xx_wordperfect_wpg_free(xx_wordperfect_wpg *);
XXFC_API bool xx_wordperfect_wpg_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_wordperfect_wpg_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
