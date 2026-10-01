/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/GNOME/gimp/master/app/core/gimppalette-load.c
 * GIMP GPL UTF8 named RGB8 palettes: complete header, optional columns and exact bounded color rows. Original descriptor and color records exported; rendering unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_GIMP_GPL_H
#define XX_GIMP_GPL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gimp_gpl {Abstractformat format;} xx_gimp_gpl;
XXFC_API void xx_gimp_gpl_init(xx_gimp_gpl *,xx_io_device *,int64_t);
XXFC_API xx_gimp_gpl *xx_gimp_gpl_create(xx_io_device *,int64_t);
XXFC_API void xx_gimp_gpl_destroy(xx_gimp_gpl *);
XXFC_API void xx_gimp_gpl_free(xx_gimp_gpl *);
XXFC_API bool xx_gimp_gpl_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gimp_gpl_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
