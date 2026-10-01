/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_LMDB_DATA_H
#define XX_LMDB_DATA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lmdb_data {Abstractformat format;} xx_lmdb_data;
XXFC_API void xx_lmdb_data_init(xx_lmdb_data *,xx_io_device *,int64_t);
XXFC_API xx_lmdb_data *xx_lmdb_data_create(xx_io_device *,int64_t);
XXFC_API void xx_lmdb_data_destroy(xx_lmdb_data *);
XXFC_API void xx_lmdb_data_free(xx_lmdb_data *);
XXFC_API bool xx_lmdb_data_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lmdb_data_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
