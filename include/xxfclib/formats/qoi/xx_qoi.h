/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://qoiformat.org/qoi-specification.pdf, https://raw.githubusercontent.com/phoboslab/qoi/master/qoi.h
 * Stored encoded component extraction; no media decoding claims.
 */
#ifndef XX_QOI_H
#define XX_QOI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_qoi { Abstractformat format; } xx_qoi;
XXFC_API void xx_qoi_init(xx_qoi *,xx_io_device *,int64_t);
XXFC_API xx_qoi *xx_qoi_create(xx_io_device *,int64_t);
XXFC_API void xx_qoi_destroy(xx_qoi *);
XXFC_API void xx_qoi_free(xx_qoi *);
XXFC_API bool xx_qoi_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_qoi_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
