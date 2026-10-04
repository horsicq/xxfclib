/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only explicit canonical one-bank Casio FZF; exact extraction scope is documented in the source.
 */
#ifndef XX_CASIO_FZF_H
#define XX_CASIO_FZF_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_casio_fzf;
XXFC_API void xx_casio_fzf_init(xx_casio_fzf *, xx_io_device *, int64_t);
XXFC_API xx_casio_fzf *xx_casio_fzf_create(xx_io_device *, int64_t);
XXFC_API void xx_casio_fzf_destroy(xx_casio_fzf *);
XXFC_API void xx_casio_fzf_free(xx_casio_fzf *);
static inline Abstractformat *xx_casio_fzf_to_format(xx_casio_fzf *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif
