/* SPDX-License-Identifier: MIT
 * Wire specification: https://manual.gromacs.org/current/reference-manual/file-formats.html#gro */
#ifndef XX_GROMACS_GRO_H
#define XX_GROMACS_GRO_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gromacs_gro { Abstractformat format; } xx_gromacs_gro;
XXFC_API void xx_gromacs_gro_init(xx_gromacs_gro *,xx_io_device *,int64_t);
XXFC_API xx_gromacs_gro *xx_gromacs_gro_create(xx_io_device *,int64_t);
XXFC_API void xx_gromacs_gro_destroy(xx_gromacs_gro *);
XXFC_API void xx_gromacs_gro_free(xx_gromacs_gro *);
XXFC_API bool xx_gromacs_gro_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gromacs_gro_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
