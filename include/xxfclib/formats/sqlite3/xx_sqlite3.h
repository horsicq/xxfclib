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
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sqlite3_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sqlite3_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sqlite3_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
