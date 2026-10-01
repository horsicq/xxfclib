/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/obspy/obspy/tree/master/obspy/io/sac */
#ifndef XX_SEISMIC_SAC_H
#define XX_SEISMIC_SAC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_seismic_sac { Abstractformat format; } xx_seismic_sac;
XXFC_API void xx_seismic_sac_init(xx_seismic_sac *,xx_io_device *,int64_t);
XXFC_API xx_seismic_sac *xx_seismic_sac_create(xx_io_device *,int64_t);
XXFC_API void xx_seismic_sac_destroy(xx_seismic_sac *);
XXFC_API void xx_seismic_sac_free(xx_seismic_sac *);
XXFC_API bool xx_seismic_sac_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_seismic_sac_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
