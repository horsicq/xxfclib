/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_LZF_STREAM_H
#define XX_LZF_STREAM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lzf_stream { Abstractformat format; } xx_lzf_stream;
XXFC_API void xx_lzf_stream_init(xx_lzf_stream *,xx_io_device *,int64_t);
XXFC_API xx_lzf_stream *xx_lzf_stream_create(xx_io_device *,int64_t);
XXFC_API void xx_lzf_stream_destroy(xx_lzf_stream *);
XXFC_API void xx_lzf_stream_free(xx_lzf_stream *);
XXFC_API bool xx_lzf_stream_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lzf_stream_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
