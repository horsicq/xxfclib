/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/openmm/openmm/master/wrappers/python/openmm/app/dcdfile.py */
#ifndef XX_CHARMM_DCD_H
#define XX_CHARMM_DCD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_charmm_dcd { Abstractformat format; } xx_charmm_dcd;
XXFC_API void xx_charmm_dcd_init(xx_charmm_dcd *,xx_io_device *,int64_t);
XXFC_API xx_charmm_dcd *xx_charmm_dcd_create(xx_io_device *,int64_t);
XXFC_API void xx_charmm_dcd_destroy(xx_charmm_dcd *);
XXFC_API void xx_charmm_dcd_free(xx_charmm_dcd *);
XXFC_API bool xx_charmm_dcd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_charmm_dcd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
