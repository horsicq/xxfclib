/* SPDX-License-Identifier: MIT */
#ifndef XX_SQLITE_SQL_H
#define XX_SQLITE_SQL_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_sqlite_sql { Abstractformat format; } xx_sqlite_sql;
XXFC_API xx_sqlite_sql *xx_sqlite_sql_create(xx_io_device *,int64_t);
XXFC_API void xx_sqlite_sql_free(xx_sqlite_sql *);
#endif
