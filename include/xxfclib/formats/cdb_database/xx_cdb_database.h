/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_CDB_DATABASE_H
#define XX_CDB_DATABASE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cdb_database {Abstractformat format;} xx_cdb_database;
XXFC_API void xx_cdb_database_init(xx_cdb_database *,xx_io_device *,int64_t);
XXFC_API xx_cdb_database *xx_cdb_database_create(xx_io_device *,int64_t);
XXFC_API void xx_cdb_database_destroy(xx_cdb_database *);
XXFC_API void xx_cdb_database_free(xx_cdb_database *);
XXFC_API bool xx_cdb_database_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cdb_database_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
