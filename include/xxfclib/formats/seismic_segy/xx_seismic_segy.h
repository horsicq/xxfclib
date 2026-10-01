/* SPDX-License-Identifier: MIT
 * Wire specification: https://seg.org/wp-content/uploads/2025/11/seg_y_rev1.pdf */
#ifndef XX_SEISMIC_SEGY_H
#define XX_SEISMIC_SEGY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_seismic_segy { Abstractformat format; } xx_seismic_segy;
XXFC_API void xx_seismic_segy_init(xx_seismic_segy *,xx_io_device *,int64_t);
XXFC_API xx_seismic_segy *xx_seismic_segy_create(xx_io_device *,int64_t);
XXFC_API void xx_seismic_segy_destroy(xx_seismic_segy *);
XXFC_API void xx_seismic_segy_free(xx_seismic_segy *);
XXFC_API bool xx_seismic_segy_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_seismic_segy_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
