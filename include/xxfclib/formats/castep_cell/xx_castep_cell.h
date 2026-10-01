/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.ase-lib.org/_modules/ase/io/castep.html */
#ifndef XX_CASTEP_CELL_H
#define XX_CASTEP_CELL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_castep_cell { Abstractformat format; } xx_castep_cell;
XXFC_API void xx_castep_cell_init(xx_castep_cell *,xx_io_device *,int64_t);
XXFC_API xx_castep_cell *xx_castep_cell_create(xx_io_device *,int64_t);
XXFC_API void xx_castep_cell_destroy(xx_castep_cell *);
XXFC_API void xx_castep_cell_free(xx_castep_cell *);
XXFC_API bool xx_castep_cell_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_castep_cell_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
