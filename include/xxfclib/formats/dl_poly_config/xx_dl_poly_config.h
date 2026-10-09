/* SPDX-License-Identifier: MIT
 * Wire specification: https://ase-lib.org/_modules/ase/io/dlp4.html */
#ifndef XX_DL_POLY_CONFIG_H
#define XX_DL_POLY_CONFIG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dl_poly_config { Abstractformat format; } xx_dl_poly_config;
XXFC_API void xx_dl_poly_config_init(xx_dl_poly_config *,xx_io_device *,int64_t);
XXFC_API xx_dl_poly_config *xx_dl_poly_config_create(xx_io_device *,int64_t);
XXFC_API void xx_dl_poly_config_destroy(xx_dl_poly_config *);
XXFC_API void xx_dl_poly_config_free(xx_dl_poly_config *);
XXFC_API bool xx_dl_poly_config_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dl_poly_config_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dl_poly_config_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dl_poly_config_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dl_poly_config_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dl_poly_config_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dl_poly_config_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
