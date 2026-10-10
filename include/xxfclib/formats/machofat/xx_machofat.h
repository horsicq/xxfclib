/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_MACHOFAT_H
#define XXFCLIB_FORMAT_MACHOFAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Universal Mach-O container. Each original architecture slice is a stored
 * archive member. Both table widths and byte orders are supported. */
typedef struct xx_machofat { Abstractformat format; } xx_machofat;
XXFC_API void xx_machofat_init(xx_machofat *, xx_io_device *, int64_t);
XXFC_API xx_machofat *xx_machofat_create(xx_io_device *, int64_t);
XXFC_API void xx_machofat_destroy(xx_machofat *);
XXFC_API void xx_machofat_free(xx_machofat *);
XXFC_API bool xx_machofat_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_machofat_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_machofat_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
