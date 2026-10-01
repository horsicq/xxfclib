/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_S98_LOG_H
#define XX_S98_LOG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_s98_log { Abstractformat format; } xx_s98_log;
XXFC_API void xx_s98_log_init(xx_s98_log *,xx_io_device *,int64_t);
XXFC_API xx_s98_log *xx_s98_log_create(xx_io_device *,int64_t);
XXFC_API void xx_s98_log_destroy(xx_s98_log *);
XXFC_API void xx_s98_log_free(xx_s98_log *);
XXFC_API bool xx_s98_log_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_s98_log_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
