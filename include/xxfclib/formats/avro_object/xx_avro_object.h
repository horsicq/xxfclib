/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_AVRO_OBJECT_H
#define XX_AVRO_OBJECT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_avro_object { Abstractformat format; } xx_avro_object;
XXFC_API void xx_avro_object_init(xx_avro_object *,xx_io_device *,int64_t);
XXFC_API xx_avro_object *xx_avro_object_create(xx_io_device *,int64_t);
XXFC_API void xx_avro_object_destroy(xx_avro_object *);
XXFC_API void xx_avro_object_free(xx_avro_object *);
XXFC_API bool xx_avro_object_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_avro_object_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
