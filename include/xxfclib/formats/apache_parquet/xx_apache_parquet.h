/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/apache/parquet-format/master/src/main/thrift/parquet.thrift
 * Bounded encoded-component extraction. */
#ifndef XX_APACHE_PARQUET_H
#define XX_APACHE_PARQUET_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_apache_parquet { Abstractformat format; } xx_apache_parquet;
XXFC_API void xx_apache_parquet_init(xx_apache_parquet *,xx_io_device *,int64_t);
XXFC_API xx_apache_parquet *xx_apache_parquet_create(xx_io_device *,int64_t);
XXFC_API void xx_apache_parquet_destroy(xx_apache_parquet *);
XXFC_API void xx_apache_parquet_free(xx_apache_parquet *);
XXFC_API bool xx_apache_parquet_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apache_parquet_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
