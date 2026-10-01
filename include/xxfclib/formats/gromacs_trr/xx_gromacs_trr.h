/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/gromacs/gromacs/main/src/gromacs/fileio/trrio.cpp */
#ifndef XX_GROMACS_TRR_H
#define XX_GROMACS_TRR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gromacs_trr { Abstractformat format; } xx_gromacs_trr;
XXFC_API void xx_gromacs_trr_init(xx_gromacs_trr *,xx_io_device *,int64_t);
XXFC_API xx_gromacs_trr *xx_gromacs_trr_create(xx_io_device *,int64_t);
XXFC_API void xx_gromacs_trr_destroy(xx_gromacs_trr *);
XXFC_API void xx_gromacs_trr_free(xx_gromacs_trr *);
XXFC_API bool xx_gromacs_trr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gromacs_trr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
