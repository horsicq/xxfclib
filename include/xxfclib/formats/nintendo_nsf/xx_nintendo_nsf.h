/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_NINTENDO_NSF_H
#define XX_NINTENDO_NSF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_nsf { Abstractformat format; } xx_nintendo_nsf;
XXFC_API void xx_nintendo_nsf_init(xx_nintendo_nsf *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_nsf *xx_nintendo_nsf_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_nsf_destroy(xx_nintendo_nsf *);
XXFC_API void xx_nintendo_nsf_free(xx_nintendo_nsf *);
XXFC_API bool xx_nintendo_nsf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_nsf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
