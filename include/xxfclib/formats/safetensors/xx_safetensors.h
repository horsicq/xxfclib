/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_SAFETENSORS_H
#define XX_SAFETENSORS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_safetensors {Abstractformat format;} xx_safetensors;
XXFC_API void xx_safetensors_init(xx_safetensors *,xx_io_device *,int64_t);
XXFC_API xx_safetensors *xx_safetensors_create(xx_io_device *,int64_t);
XXFC_API void xx_safetensors_destroy(xx_safetensors *);
XXFC_API void xx_safetensors_free(xx_safetensors *);
XXFC_API bool xx_safetensors_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_safetensors_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
