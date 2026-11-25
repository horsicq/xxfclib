/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * MenuetOS/KolibriOS MENUET00, MENUET01 and MENUET02 executables.
 */
#ifndef XX_MENUET_H
#define XX_MENUET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_menuet {
    Abstractformat format;
    uint8_t header_version;
    uint32_t entry_point;
    uint32_t image_size;
    uint32_t memory_size;
} xx_menuet;
XXFC_API void xx_menuet_init(xx_menuet *, xx_io_device *, int64_t);
XXFC_API xx_menuet *xx_menuet_create(xx_io_device *, int64_t);
XXFC_API void xx_menuet_destroy(xx_menuet *);
XXFC_API void xx_menuet_free(xx_menuet *);
XXFC_API bool xx_menuet_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_menuet_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_menuet_get_memory_map(Abstractformat *, xx_memory_map_mode_t, xx_memory_map *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
