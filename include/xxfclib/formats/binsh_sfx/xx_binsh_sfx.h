/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_BINSH_SFX_H
#define XXFCLIB_FORMAT_BINSH_SFX_H
#include "xxfclib/formats/sun_java_binsh/xx_sun_java_binsh.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Generic authenticated shell tail-carve archive, sharing the Sun reader.
 * Shell directives are parsed as bytes and never executed. */
typedef xx_sun_java_binsh xx_binsh_sfx;
XXFC_API void xx_binsh_sfx_init(xx_binsh_sfx *, xx_io_device *, int64_t);
XXFC_API xx_binsh_sfx *xx_binsh_sfx_create(xx_io_device *, int64_t);
XXFC_API void xx_binsh_sfx_destroy(xx_binsh_sfx *);
XXFC_API void xx_binsh_sfx_free(xx_binsh_sfx *);
XXFC_API bool xx_binsh_sfx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_binsh_sfx_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_binsh_sfx_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
