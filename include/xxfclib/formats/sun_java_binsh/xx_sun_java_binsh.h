/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/installers/xbinshsfx.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_SUN_JAVA_BINSH_H
#define XX_SUN_JAVA_BINSH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sun_java_binsh { Abstractformat format; } xx_sun_java_binsh;
XXFC_API void xx_sun_java_binsh_init(xx_sun_java_binsh *,xx_io_device *,int64_t);
XXFC_API xx_sun_java_binsh *xx_sun_java_binsh_create(xx_io_device *,int64_t);
XXFC_API void xx_sun_java_binsh_destroy(xx_sun_java_binsh *);
XXFC_API void xx_sun_java_binsh_free(xx_sun_java_binsh *);
XXFC_API bool xx_sun_java_binsh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sun_java_binsh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
