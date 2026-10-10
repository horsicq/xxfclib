/* SPDX-License-Identifier: MIT
 * Wire specification: https://codes.ecmwf.int/grib/format/grib2/regulations/ */
#ifndef XX_WMO_GRIB_H
#define XX_WMO_GRIB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wmo_grib {
    Abstractformat format;
} xx_wmo_grib;
XXFC_API void xx_wmo_grib_init(xx_wmo_grib *, xx_io_device *, int64_t);
XXFC_API xx_wmo_grib *xx_wmo_grib_create(xx_io_device *, int64_t);
XXFC_API void xx_wmo_grib_destroy(xx_wmo_grib *);
XXFC_API void xx_wmo_grib_free(xx_wmo_grib *);
XXFC_API bool xx_wmo_grib_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_wmo_grib_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_wmo_grib_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_wmo_grib_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_wmo_grib_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_wmo_grib_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_wmo_grib_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
