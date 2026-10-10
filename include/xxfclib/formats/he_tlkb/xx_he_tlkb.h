/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_HE_TLKB_H
#define XX_HE_TLKB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_he_tlkb { Abstractformat format; } xx_he_tlkb;
/* The whole container, including each member payload, is XORed with 0x69. */
enum { XX_HE_TLKB_METHOD_XOR_69 = 0x69 };

XXFC_API void xx_he_tlkb_init(xx_he_tlkb *, xx_io_device *, int64_t);
XXFC_API xx_he_tlkb *xx_he_tlkb_create(xx_io_device *, int64_t);
XXFC_API void xx_he_tlkb_destroy(xx_he_tlkb *);
XXFC_API void xx_he_tlkb_free(xx_he_tlkb *);
XXFC_API xx_file_type_t xx_he_tlkb_detect(xx_io_device *, int64_t);
static inline Abstractformat *xx_he_tlkb_to_format(xx_he_tlkb *r) {
    return r ? &r->format : NULL;
}

#ifdef __cplusplus
}
#endif
#endif
