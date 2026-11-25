/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_RZIP_H
#define XXFCLIB_FORMAT_RZIP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** RZIP 2.0/2.1 two-stream archive. Stored/BZip2 blocks and chunk CRCs. */
typedef struct xx_rzip {
    Abstractformat format;
} xx_rzip;
XXFC_API void xx_rzip_init(xx_rzip *, xx_io_device *, int64_t);
XXFC_API xx_rzip *xx_rzip_create(xx_io_device *, int64_t);
XXFC_API void xx_rzip_destroy(xx_rzip *);
XXFC_API void xx_rzip_free(xx_rzip *);
XXFC_API bool xx_rzip_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_rzip_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API xx_file_type_t xx_rzip_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
