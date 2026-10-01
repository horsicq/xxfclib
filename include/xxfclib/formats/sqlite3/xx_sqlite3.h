/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SQLITE3_H
#define XX_SQLITE3_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sqlite3 { Abstractformat format; } xx_sqlite3;
XXFC_API void xx_sqlite3_init(xx_sqlite3 *,xx_io_device *,int64_t);
XXFC_API xx_sqlite3 *xx_sqlite3_create(xx_io_device *,int64_t);
XXFC_API void xx_sqlite3_destroy(xx_sqlite3 *);
XXFC_API void xx_sqlite3_free(xx_sqlite3 *);
XXFC_API bool xx_sqlite3_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sqlite3_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
