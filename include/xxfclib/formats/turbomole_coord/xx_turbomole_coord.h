/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.ase-lib.org/_modules/ase/io/turbomole.html */
#ifndef XX_TURBOMOLE_COORD_H
#define XX_TURBOMOLE_COORD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_turbomole_coord { Abstractformat format; } xx_turbomole_coord;
XXFC_API void xx_turbomole_coord_init(xx_turbomole_coord *,xx_io_device *,int64_t);
XXFC_API xx_turbomole_coord *xx_turbomole_coord_create(xx_io_device *,int64_t);
XXFC_API void xx_turbomole_coord_destroy(xx_turbomole_coord *);
XXFC_API void xx_turbomole_coord_free(xx_turbomole_coord *);
XXFC_API bool xx_turbomole_coord_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_turbomole_coord_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
