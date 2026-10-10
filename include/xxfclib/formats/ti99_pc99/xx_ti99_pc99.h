/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only TI PC99 tokenized track image; exact extraction scope is documented in the source.
 */
#ifndef XX_TI99_PC99_H
#define XX_TI99_PC99_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_ti99_pc99;
XXFC_API void xx_ti99_pc99_init(xx_ti99_pc99 *, xx_io_device *, int64_t);
XXFC_API xx_ti99_pc99 *xx_ti99_pc99_create(xx_io_device *, int64_t);
XXFC_API void xx_ti99_pc99_destroy(xx_ti99_pc99 *);
XXFC_API void xx_ti99_pc99_free(xx_ti99_pc99 *);
static inline Abstractformat *xx_ti99_pc99_to_format(xx_ti99_pc99 *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
