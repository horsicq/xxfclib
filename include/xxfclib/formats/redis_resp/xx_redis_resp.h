/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_REDIS_RESP_H
#define XX_REDIS_RESP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_redis_resp { Abstractformat format; } xx_redis_resp;
XXFC_API void xx_redis_resp_init(xx_redis_resp *,xx_io_device *,int64_t);
XXFC_API xx_redis_resp *xx_redis_resp_create(xx_io_device *,int64_t);
XXFC_API void xx_redis_resp_destroy(xx_redis_resp *);
XXFC_API void xx_redis_resp_free(xx_redis_resp *);
XXFC_API bool xx_redis_resp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_redis_resp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
