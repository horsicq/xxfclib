/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/svi-opensource/libics */
#ifndef XX_MICROSCOPY_ICS_H
#define XX_MICROSCOPY_ICS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_microscopy_ics { Abstractformat format; } xx_microscopy_ics;
XXFC_API void xx_microscopy_ics_init(xx_microscopy_ics *,xx_io_device *,int64_t);
XXFC_API xx_microscopy_ics *xx_microscopy_ics_create(xx_io_device *,int64_t);
XXFC_API void xx_microscopy_ics_destroy(xx_microscopy_ics *);
XXFC_API void xx_microscopy_ics_free(xx_microscopy_ics *);
XXFC_API bool xx_microscopy_ics_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_microscopy_ics_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
