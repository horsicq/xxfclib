/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Microsoft MS-MPI setup bootstrapper (msmpisetup.exe): whole files
 * (Windows Installer packages and helper executables) appended to the stub
 * and separated by 512-byte marker blocks of the u32 0x0FABFAB0.
 */
#ifndef XX_MSMPI_BOOTSTRAPPER_H
#define XX_MSMPI_BOOTSTRAPPER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_msmpi_bootstrapper {
    Abstractformat format;
} xx_msmpi_bootstrapper;
XXFC_API void xx_msmpi_bootstrapper_init(xx_msmpi_bootstrapper *, xx_io_device *, int64_t);
XXFC_API xx_msmpi_bootstrapper *xx_msmpi_bootstrapper_create(xx_io_device *, int64_t);
XXFC_API void xx_msmpi_bootstrapper_destroy(xx_msmpi_bootstrapper *);
XXFC_API void xx_msmpi_bootstrapper_free(xx_msmpi_bootstrapper *);
XXFC_API bool xx_msmpi_bootstrapper_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_msmpi_bootstrapper_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_msmpi_bootstrapper_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_msmpi_bootstrapper_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_msmpi_bootstrapper_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_msmpi_bootstrapper_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_msmpi_bootstrapper_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
