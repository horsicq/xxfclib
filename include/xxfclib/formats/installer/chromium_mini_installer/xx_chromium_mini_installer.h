/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Chromium mini_installer (Google Chrome, Microsoft Edge, Brave ...): the
 * setup archive is an LZMA-compressed resource of type "BL" (7-Zip inside)
 * or "B" (Brave: a size table and a tar inside).
 */
#ifndef XX_CHROMIUM_MINI_INSTALLER_H
#define XX_CHROMIUM_MINI_INSTALLER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_chromium_mini_installer {
    Abstractformat format;
    void *nested; /* The decoded payload's archive reader. */
} xx_chromium_mini_installer;
XXFC_API void xx_chromium_mini_installer_init(xx_chromium_mini_installer *, xx_io_device *, int64_t);
XXFC_API xx_chromium_mini_installer *xx_chromium_mini_installer_create(xx_io_device *, int64_t);
XXFC_API void xx_chromium_mini_installer_destroy(xx_chromium_mini_installer *);
XXFC_API void xx_chromium_mini_installer_free(xx_chromium_mini_installer *);
XXFC_API bool xx_chromium_mini_installer_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_chromium_mini_installer_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_chromium_mini_installer_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_chromium_mini_installer_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_chromium_mini_installer_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_chromium_mini_installer_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_chromium_mini_installer_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
