/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_REDIS_RDB_H
#define XX_REDIS_RDB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_redis_rdb { Abstractformat format; } xx_redis_rdb;
XXFC_API void xx_redis_rdb_init(xx_redis_rdb *,xx_io_device *,int64_t);
XXFC_API xx_redis_rdb *xx_redis_rdb_create(xx_io_device *,int64_t);
XXFC_API void xx_redis_rdb_destroy(xx_redis_rdb *);
XXFC_API void xx_redis_rdb_free(xx_redis_rdb *);
XXFC_API bool xx_redis_rdb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_redis_rdb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
