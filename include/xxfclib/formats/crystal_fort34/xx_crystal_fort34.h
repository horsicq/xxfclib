/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.ase-lib.org/_modules/ase/io/crystal.html */
#ifndef XX_CRYSTAL_FORT34_H
#define XX_CRYSTAL_FORT34_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_crystal_fort34 { Abstractformat format; } xx_crystal_fort34;
XXFC_API void xx_crystal_fort34_init(xx_crystal_fort34 *,xx_io_device *,int64_t);
XXFC_API xx_crystal_fort34 *xx_crystal_fort34_create(xx_io_device *,int64_t);
XXFC_API void xx_crystal_fort34_destroy(xx_crystal_fort34 *);
XXFC_API void xx_crystal_fort34_free(xx_crystal_fort34 *);
XXFC_API bool xx_crystal_fort34_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_crystal_fort34_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
