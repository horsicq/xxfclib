/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/ecmwf/eccodes/tree/develop/definitions/bufr */
#ifndef XX_WMO_BUFR_H
#define XX_WMO_BUFR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wmo_bufr { Abstractformat format; } xx_wmo_bufr;
XXFC_API void xx_wmo_bufr_init(xx_wmo_bufr *,xx_io_device *,int64_t);
XXFC_API xx_wmo_bufr *xx_wmo_bufr_create(xx_io_device *,int64_t);
XXFC_API void xx_wmo_bufr_destroy(xx_wmo_bufr *);
XXFC_API void xx_wmo_bufr_free(xx_wmo_bufr *);
XXFC_API bool xx_wmo_bufr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_wmo_bufr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
