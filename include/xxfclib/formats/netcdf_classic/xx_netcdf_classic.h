/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.unidata.ucar.edu/netcdf-c/4.9.2/file_format_specifications.html
 * Bounded encoded-component extraction. */
#ifndef XX_NETCDF_CLASSIC_H
#define XX_NETCDF_CLASSIC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_netcdf_classic { Abstractformat format; } xx_netcdf_classic;
XXFC_API void xx_netcdf_classic_init(xx_netcdf_classic *,xx_io_device *,int64_t);
XXFC_API xx_netcdf_classic *xx_netcdf_classic_create(xx_io_device *,int64_t);
XXFC_API void xx_netcdf_classic_destroy(xx_netcdf_classic *);
XXFC_API void xx_netcdf_classic_free(xx_netcdf_classic *);
XXFC_API bool xx_netcdf_classic_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_netcdf_classic_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_netcdf_classic_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_netcdf_classic_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_netcdf_classic_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_netcdf_classic_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_netcdf_classic_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
