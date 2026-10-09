/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_MYSQL_BINLOG_H
#define XX_MYSQL_BINLOG_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mysql_binlog { Abstractformat format; } xx_mysql_binlog;
XXFC_API void xx_mysql_binlog_init(xx_mysql_binlog *,xx_io_device *,int64_t);
XXFC_API xx_mysql_binlog *xx_mysql_binlog_create(xx_io_device *,int64_t);
XXFC_API void xx_mysql_binlog_destroy(xx_mysql_binlog *);
XXFC_API void xx_mysql_binlog_free(xx_mysql_binlog *);
XXFC_API bool xx_mysql_binlog_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mysql_binlog_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mysql_binlog_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mysql_binlog_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mysql_binlog_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mysql_binlog_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mysql_binlog_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
