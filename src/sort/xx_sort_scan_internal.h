/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_SORT_SCAN_INTERNAL_H
#define XX_SORT_SCAN_INTERNAL_H

#include "xxfclib/scan/xx_scan.h"
#include "xxfclib/die_engine/die_engine_db.h"

/* Preserve database load ordering without changing the public db_sort no-op. */
void xx_scan_sort_database_records(DBase *database);

#endif /* XX_SORT_SCAN_INTERNAL_H */
