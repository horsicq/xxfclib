/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent primary-layout implementation; stored encoded components only.
 */
/* Layout: https://raw.githubusercontent.com/lsp-plugins/lsp-3rd-party/master/include/steinberg/vst2.h
 * Big-endian VST2 CcnK FxCk/FPCh presets and FxBk/FBCh banks, bank versions1/2 and presetversion1. Up to1024 programs/65536 normalized finite parameters and256MiB opaque plugin data. Validates signed chunk sizes, identities, program counts/current program and exact nesting. Exports descriptors, parameter arrays or declared opaque plugin state without interpreting/loading plugins. A standalone FPCh zero outer byteSize is accepted only with a complete signed inner chunk length, as emitted by Surge XT. Unknown versions/extensions rejected.
 */
#ifndef XX_STEINBERG_FXB_H
#define XX_STEINBERG_FXB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_steinberg_fxb { Abstractformat format; } xx_steinberg_fxb;
XXFC_API void xx_steinberg_fxb_init(xx_steinberg_fxb *,xx_io_device *,int64_t);
XXFC_API xx_steinberg_fxb *xx_steinberg_fxb_create(xx_io_device *,int64_t);
XXFC_API void xx_steinberg_fxb_destroy(xx_steinberg_fxb *);
XXFC_API void xx_steinberg_fxb_free(xx_steinberg_fxb *);
XXFC_API bool xx_steinberg_fxb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_steinberg_fxb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
