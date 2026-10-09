/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/PicoQuant/PicoQuant-Time-Tagged-File-Format-Demos */
#ifndef XX_PHOTONTIMING_PTU_H
#define XX_PHOTONTIMING_PTU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_photontiming_ptu { Abstractformat format; } xx_photontiming_ptu;
XXFC_API void xx_photontiming_ptu_init(xx_photontiming_ptu *,xx_io_device *,int64_t);
XXFC_API xx_photontiming_ptu *xx_photontiming_ptu_create(xx_io_device *,int64_t);
XXFC_API void xx_photontiming_ptu_destroy(xx_photontiming_ptu *);
XXFC_API void xx_photontiming_ptu_free(xx_photontiming_ptu *);
XXFC_API bool xx_photontiming_ptu_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_photontiming_ptu_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_photontiming_ptu_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_photontiming_ptu_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_photontiming_ptu_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_photontiming_ptu_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_photontiming_ptu_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
