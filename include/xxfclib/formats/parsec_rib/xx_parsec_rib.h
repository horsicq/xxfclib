/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PARSEC_RIB_H
#define XX_PARSEC_RIB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RIB\0 reverse-decoded Parsec resource stream. */
typedef struct xx_parsec_rib { Abstractformat format; } xx_parsec_rib;

XXFC_API void xx_parsec_rib_init(xx_parsec_rib *, xx_io_device *, int64_t);
XXFC_API xx_parsec_rib *xx_parsec_rib_create(xx_io_device *, int64_t);
XXFC_API void xx_parsec_rib_destroy(xx_parsec_rib *);
XXFC_API void xx_parsec_rib_free(xx_parsec_rib *);

#ifdef __cplusplus
}
#endif
#endif
