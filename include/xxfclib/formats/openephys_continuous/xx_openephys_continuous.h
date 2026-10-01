/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/open-ephys/analysis-tools/blob/master/OpenEphys.py */
#ifndef XX_OPENEPHYS_CONTINUOUS_H
#define XX_OPENEPHYS_CONTINUOUS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openephys_continuous { Abstractformat format; } xx_openephys_continuous;
XXFC_API void xx_openephys_continuous_init(xx_openephys_continuous *,xx_io_device *,int64_t);
XXFC_API xx_openephys_continuous *xx_openephys_continuous_create(xx_io_device *,int64_t);
XXFC_API void xx_openephys_continuous_destroy(xx_openephys_continuous *);
XXFC_API void xx_openephys_continuous_free(xx_openephys_continuous *);
XXFC_API bool xx_openephys_continuous_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_openephys_continuous_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
