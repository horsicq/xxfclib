/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_LDBST_H
#define XX_LDBST_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Bounded native parser for LibDsk's [LDBS] text disc-image form.
 * Healthy regular FM/MFM sectors are reconstructed as disk.img. */
typedef struct xx_ldbst_s {
    Abstractformat format;
} xx_ldbst;
XXFC_API void xx_ldbst_init(xx_ldbst *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_ldbst *xx_ldbst_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ldbst_destroy(xx_ldbst *reader);
XXFC_API void xx_ldbst_free(xx_ldbst *reader);
XXFC_API bool xx_ldbst_unpack_to_device(xx_ldbst *reader, xx_io_device *destination, xx_pd_struct *pd);
#ifdef __cplusplus
}
#endif
#endif
