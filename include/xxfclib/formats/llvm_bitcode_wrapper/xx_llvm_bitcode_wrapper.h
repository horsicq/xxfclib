/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. Version 0 wrapper; exports its declared raw BC stream. No LLVM IR semantic decoding.
 */
#ifndef XX_LLVM_BITCODE_WRAPPER_H
#define XX_LLVM_BITCODE_WRAPPER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_llvm_bitcode_wrapper { Abstractformat format; } xx_llvm_bitcode_wrapper;
XXFC_API void xx_llvm_bitcode_wrapper_init(xx_llvm_bitcode_wrapper *,xx_io_device *,int64_t);
XXFC_API xx_llvm_bitcode_wrapper *xx_llvm_bitcode_wrapper_create(xx_io_device *,int64_t);
XXFC_API void xx_llvm_bitcode_wrapper_destroy(xx_llvm_bitcode_wrapper *);
XXFC_API void xx_llvm_bitcode_wrapper_free(xx_llvm_bitcode_wrapper *);
XXFC_API bool xx_llvm_bitcode_wrapper_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_llvm_bitcode_wrapper_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
