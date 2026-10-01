/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * IVF (DKIF) video packet container reader.
 * Format reference: https://github.com/webmproject/libvpx/blob/main/ivfenc.c
 * Decoder reference: https://github.com/webmproject/libvpx/blob/main/ivfdec.c
 */
#ifndef XX_IVF_H
#define XX_IVF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ivf { Abstractformat format; } xx_ivf;

XXFC_API void xx_ivf_init(xx_ivf *, xx_io_device *, int64_t);
XXFC_API xx_ivf *xx_ivf_create(xx_io_device *, int64_t);
XXFC_API void xx_ivf_destroy(xx_ivf *);
XXFC_API void xx_ivf_free(xx_ivf *);
XXFC_API bool xx_ivf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ivf_handle_base_info(Abstractformat *, xx_pd_struct *);

#ifdef __cplusplus
}
#endif
#endif
