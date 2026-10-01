/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/sqlite/sqlite/blob/master/src/pager.c */
#ifndef XX_SQLITE_ROLLBACK_JOURNAL_H
#define XX_SQLITE_ROLLBACK_JOURNAL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sqlite_rollback_journal { Abstractformat format; } xx_sqlite_rollback_journal;
XXFC_API void xx_sqlite_rollback_journal_init(xx_sqlite_rollback_journal *,xx_io_device *,int64_t);
XXFC_API xx_sqlite_rollback_journal *xx_sqlite_rollback_journal_create(xx_io_device *,int64_t);
XXFC_API void xx_sqlite_rollback_journal_destroy(xx_sqlite_rollback_journal *);
XXFC_API void xx_sqlite_rollback_journal_free(xx_sqlite_rollback_journal *);
XXFC_API bool xx_sqlite_rollback_journal_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sqlite_rollback_journal_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
