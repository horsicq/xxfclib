/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_EXECUTABLE_OVERLAY_H
#define XX_EXECUTABLE_OVERLAY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/* A stored-member view of appended executable images and their remaining
 * bytes. This reader does not decompress packed executable machine code. */
typedef struct xx_executable_overlay {
    Abstractformat format;
} xx_executable_overlay;
XXFC_API void xx_executable_overlay_init(xx_executable_overlay *, xx_io_device *, int64_t);
XXFC_API xx_executable_overlay *xx_executable_overlay_create(xx_io_device *, int64_t);
XXFC_API xx_executable_overlay *xx_executable_overlay_new(xx_io_device *);
XXFC_API void xx_executable_overlay_destroy(xx_executable_overlay *);
XXFC_API void xx_executable_overlay_free(xx_executable_overlay *);
XXFC_API bool xx_executable_overlay_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_executable_overlay_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_executable_overlay_unpack_current_archive_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
