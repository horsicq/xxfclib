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
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_wmo_bufr_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_wmo_bufr_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_wmo_bufr_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
