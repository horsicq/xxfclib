/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * OS/2 RSFX carrier with a bounded RAR 1.5 member stream.
 */
#ifndef XX_SFX_RSFX_H
#define XX_SFX_RSFX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
struct xx_rar;
typedef struct xx_sfx_rsfx {
    Abstractformat format;
    struct xx_rar *inner;
    xx_io_device *normalized_device;
    uint8_t *normalized_data;
} xx_sfx_rsfx;
XXFC_API void xx_sfx_rsfx_init(xx_sfx_rsfx *, xx_io_device *, int64_t);
XXFC_API xx_sfx_rsfx *xx_sfx_rsfx_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_rsfx_destroy(xx_sfx_rsfx *);
XXFC_API void xx_sfx_rsfx_free(xx_sfx_rsfx *);
XXFC_API bool xx_sfx_rsfx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_rsfx_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
