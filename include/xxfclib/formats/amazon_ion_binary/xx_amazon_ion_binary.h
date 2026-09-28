/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_AMAZON_ION_BINARY_H
#define XX_AMAZON_ION_BINARY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amazon_ion_binary { Abstractformat format; } xx_amazon_ion_binary;
XXFC_API void xx_amazon_ion_binary_init(xx_amazon_ion_binary *,xx_io_device *,int64_t);
XXFC_API xx_amazon_ion_binary *xx_amazon_ion_binary_create(xx_io_device *,int64_t);
XXFC_API void xx_amazon_ion_binary_destroy(xx_amazon_ion_binary *);
XXFC_API void xx_amazon_ion_binary_free(xx_amazon_ion_binary *);
XXFC_API bool xx_amazon_ion_binary_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amazon_ion_binary_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
