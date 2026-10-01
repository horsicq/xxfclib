/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/WizardMac/ReadStat/master/src/spss/readstat_sav_read.c
 * Bounded encoded-component extraction. */
#ifndef XX_SPSS_SAV_H
#define XX_SPSS_SAV_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_spss_sav { Abstractformat format; } xx_spss_sav;
XXFC_API void xx_spss_sav_init(xx_spss_sav *,xx_io_device *,int64_t);
XXFC_API xx_spss_sav *xx_spss_sav_create(xx_io_device *,int64_t);
XXFC_API void xx_spss_sav_destroy(xx_spss_sav *);
XXFC_API void xx_spss_sav_free(xx_spss_sav *);
XXFC_API bool xx_spss_sav_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_spss_sav_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
