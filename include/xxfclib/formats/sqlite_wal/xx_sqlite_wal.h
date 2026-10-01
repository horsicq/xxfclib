/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SQLITE_WAL_H
#define XX_SQLITE_WAL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sqlite_wal { Abstractformat format; } xx_sqlite_wal;
XXFC_API void xx_sqlite_wal_init(xx_sqlite_wal *,xx_io_device *,int64_t);
XXFC_API xx_sqlite_wal *xx_sqlite_wal_create(xx_io_device *,int64_t);
XXFC_API void xx_sqlite_wal_destroy(xx_sqlite_wal *);
XXFC_API void xx_sqlite_wal_free(xx_sqlite_wal *);
XXFC_API bool xx_sqlite_wal_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sqlite_wal_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
