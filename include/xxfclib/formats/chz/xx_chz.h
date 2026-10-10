/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_CHZ_H
#define XXFCLIB_FORMAT_CHZ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Raw ChArc record chain. Stored and ChArc-compressed members. */
typedef struct xx_chz {
    Abstractformat format;
} xx_chz;
XXFC_API void xx_chz_init(xx_chz *, xx_io_device *, int64_t);
XXFC_API xx_chz *xx_chz_create(xx_io_device *, int64_t);
XXFC_API void xx_chz_destroy(xx_chz *);
XXFC_API void xx_chz_free(xx_chz *);
XXFC_API bool xx_chz_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_chz_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_chz_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
