/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_OPENZIM_H
#define XX_OPENZIM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openzim { Abstractformat format; } xx_openzim;
XXFC_API void xx_openzim_init(xx_openzim *,xx_io_device *,int64_t);
XXFC_API xx_openzim *xx_openzim_create(xx_io_device *,int64_t);
XXFC_API void xx_openzim_destroy(xx_openzim *);
XXFC_API void xx_openzim_free(xx_openzim *);
XXFC_API bool xx_openzim_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_openzim_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
