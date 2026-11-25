/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_LEGACY_SOUND_DRIVER_H
#define XX_LEGACY_SOUND_DRIVER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_legacy_sound_driver {
    Abstractformat format;
} xx_legacy_sound_driver;
XXFC_API void xx_legacy_sound_driver_init(xx_legacy_sound_driver *, xx_io_device *, int64_t, xx_file_type_t, const char *);
XXFC_API void xx_legacy_sound_driver_destroy(xx_legacy_sound_driver *);
XXFC_API xx_file_type_t xx_legacy_sound_driver_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
