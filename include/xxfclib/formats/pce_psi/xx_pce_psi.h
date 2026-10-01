/* SPDX-License-Identifier: MIT */
#ifndef XX_PCE_PSI_H
#define XX_PCE_PSI_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_pce_psi {Abstractformat format;} xx_pce_psi;
XXFC_API void xx_pce_psi_init(xx_pce_psi *,xx_io_device *,int64_t);
XXFC_API xx_pce_psi *xx_pce_psi_create(xx_io_device *,int64_t);
XXFC_API void xx_pce_psi_destroy(xx_pce_psi *);
XXFC_API void xx_pce_psi_free(xx_pce_psi *);
XXFC_API bool xx_pce_psi_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pce_psi_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
