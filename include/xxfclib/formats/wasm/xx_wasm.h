/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. Core version 1 sections 0-12; validates framing, order and uniqueness. Exports encoded sections; no instruction validation or execution.
 */
#ifndef XX_WASM_H
#define XX_WASM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wasm { Abstractformat format; } xx_wasm;
XXFC_API void xx_wasm_init(xx_wasm *,xx_io_device *,int64_t);
XXFC_API xx_wasm *xx_wasm_create(xx_io_device *,int64_t);
XXFC_API void xx_wasm_destroy(xx_wasm *);
XXFC_API void xx_wasm_free(xx_wasm *);
XXFC_API bool xx_wasm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_wasm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
