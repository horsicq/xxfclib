/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * InstallShield InstallScript / InstallScript-MSI "setup.exe" launcher
 * (2012 .. 2023, also the setupPreReq prerequisite launcher): the Disk1
 * files ride in the PE overlay behind an "InstallShield" tag, each
 * obfuscated with a key made of its own name and optionally zlib-packed.
 */
#ifndef XX_INSTALLSHIELD_SCRIPT_SETUP_H
#define XX_INSTALLSHIELD_SCRIPT_SETUP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_installshield_script_setup {
    Abstractformat format;
} xx_installshield_script_setup;
XXFC_API void xx_installshield_script_setup_init(xx_installshield_script_setup *, xx_io_device *, int64_t);
XXFC_API xx_installshield_script_setup *xx_installshield_script_setup_create(xx_io_device *, int64_t);
XXFC_API void xx_installshield_script_setup_destroy(xx_installshield_script_setup *);
XXFC_API void xx_installshield_script_setup_free(xx_installshield_script_setup *);
XXFC_API bool xx_installshield_script_setup_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_installshield_script_setup_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_installshield_script_setup_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_installshield_script_setup_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_installshield_script_setup_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_installshield_script_setup_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_installshield_script_setup_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
