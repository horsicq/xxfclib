/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_THRIFT_COMPACT_H
#define XX_THRIFT_COMPACT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_thrift_compact { Abstractformat format; } xx_thrift_compact;
XXFC_API void xx_thrift_compact_init(xx_thrift_compact *,xx_io_device *,int64_t);
XXFC_API xx_thrift_compact *xx_thrift_compact_create(xx_io_device *,int64_t);
XXFC_API void xx_thrift_compact_destroy(xx_thrift_compact *);
XXFC_API void xx_thrift_compact_free(xx_thrift_compact *);
XXFC_API bool xx_thrift_compact_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_thrift_compact_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
