/* SPDX-License-Identifier: MIT
 * Wire specification: https://codes.ecmwf.int/grib/format/grib2/regulations/ */
#ifndef XX_WMO_GRIB_H
#define XX_WMO_GRIB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wmo_grib { Abstractformat format; } xx_wmo_grib;
XXFC_API void xx_wmo_grib_init(xx_wmo_grib *,xx_io_device *,int64_t);
XXFC_API xx_wmo_grib *xx_wmo_grib_create(xx_io_device *,int64_t);
XXFC_API void xx_wmo_grib_destroy(xx_wmo_grib *);
XXFC_API void xx_wmo_grib_free(xx_wmo_grib *);
XXFC_API bool xx_wmo_grib_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_wmo_grib_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
