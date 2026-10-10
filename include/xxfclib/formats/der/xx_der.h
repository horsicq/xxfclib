/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_DER_H
#define XX_DER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_der {
    Abstractformat format;
} xx_der;
typedef struct xx_der_header {
    uint8_t tag;
    int64_t header_size, content_offset, content_size;
} xx_der_header;
XXFC_API void xx_der_init(xx_der *, xx_io_device *, int64_t);
XXFC_API xx_der *xx_der_create(xx_io_device *, int64_t);
XXFC_API void xx_der_destroy(xx_der *);
XXFC_API void xx_der_free(xx_der *);
/* A bounded top-level definite-length envelope, as in Formats XDER.
 * This does not assert canonical DER encoding or validate the value tree. */
XXFC_API bool xx_der_get_header(xx_der *, xx_der_header *, xx_pd_struct *);
static inline Abstractformat *xx_der_to_format(xx_der *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
