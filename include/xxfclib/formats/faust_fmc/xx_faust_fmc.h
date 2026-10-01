/* SPDX-License-Identifier: MIT */
#ifndef XX_FAUST_FMC_H
#define XX_FAUST_FMC_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_faust_fmc {Abstractformat format;} xx_faust_fmc;
XXFC_API void xx_faust_fmc_init(xx_faust_fmc *,xx_io_device *,int64_t);
XXFC_API xx_faust_fmc *xx_faust_fmc_create(xx_io_device *,int64_t);
XXFC_API void xx_faust_fmc_destroy(xx_faust_fmc *);
XXFC_API void xx_faust_fmc_free(xx_faust_fmc *);
XXFC_API bool xx_faust_fmc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_faust_fmc_handle_base_info(Abstractformat *,xx_pd_struct *);
#endif
