/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_VGM_LOG_H
#define XX_VGM_LOG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_vgm_log { Abstractformat format; } xx_vgm_log;
XXFC_API void xx_vgm_log_init(xx_vgm_log *,xx_io_device *,int64_t);
XXFC_API xx_vgm_log *xx_vgm_log_create(xx_io_device *,int64_t);
XXFC_API void xx_vgm_log_destroy(xx_vgm_log *);
XXFC_API void xx_vgm_log_free(xx_vgm_log *);
XXFC_API bool xx_vgm_log_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_vgm_log_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
