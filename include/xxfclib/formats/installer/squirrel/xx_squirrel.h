/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Squirrel.Windows Setup.exe: the application's NuGet package (.nupkg) and
 * its RELEASES index travel as a ZIP inside a resource of the setup stub.
 */
#ifndef XX_SQUIRREL_H
#define XX_SQUIRREL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_squirrel {
    Abstractformat format;
    void *nested; /* Validated ZIP readers over the resource payloads. */
} xx_squirrel;
XXFC_API void xx_squirrel_init(xx_squirrel *, xx_io_device *, int64_t);
XXFC_API xx_squirrel *xx_squirrel_create(xx_io_device *, int64_t);
XXFC_API void xx_squirrel_destroy(xx_squirrel *);
XXFC_API void xx_squirrel_free(xx_squirrel *);
XXFC_API bool xx_squirrel_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_squirrel_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_squirrel_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_squirrel_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_squirrel_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_squirrel_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_squirrel_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
