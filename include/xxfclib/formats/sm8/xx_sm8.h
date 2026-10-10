/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_SM8_H
#define XX_SM8_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sm8 { Abstractformat format; } xx_sm8;
XXFC_API void xx_sm8_init(xx_sm8 *, xx_io_device *, int64_t);
XXFC_API xx_sm8 *xx_sm8_create(xx_io_device *, int64_t);
XXFC_API void xx_sm8_destroy(xx_sm8 *);
XXFC_API void xx_sm8_free(xx_sm8 *);
XXFC_API bool xx_sm8_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sm8_handle_base_info(Abstractformat *, xx_pd_struct *);
/* Structural detection preserves the input device cursor. */
XXFC_API xx_file_type_t xx_sm8_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
